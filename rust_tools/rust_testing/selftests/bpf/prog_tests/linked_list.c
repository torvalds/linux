// SPDX-License-Identifier: GPL-2.0
#include <bpf/btf.h>
#include <test_btf.h>
#include <linux/btf.h>
#include <test_progs.h>
#include <network_helpers.h>

#include "linked_list.skel.h"
#include "linked_list_fail.skel.h"
#include "linked_list_peek.skel.h"

static char log_buf[1024 * 1024];

static struct {
	const char *prog_name;
	const char *err_msg;
} linked_list_fail_tests[] = {
#define TEST(test, off) \
	{ #test "_missing_lock_push_front", \
	  "bpf_spin_lock at off=" #off " must be held for bpf_list_head" }, \
	{ #test "_missing_lock_push_back", \
	  "bpf_spin_lock at off=" #off " must be held for bpf_list_head" }, \
	{ #test "_missing_lock_pop_front", \
	  "bpf_spin_lock at off=" #off " must be held for bpf_list_head" }, \
	{ #test "_missing_lock_pop_back", \
	  "bpf_spin_lock at off=" #off " must be held for bpf_list_head" },
	TEST(kptr, 40)
	TEST(global, 16)
	TEST(map, 0)
	TEST(inner_map, 0)
#undef TEST
#define TEST(test, op) \
	{ #test "_kptr_incorrect_lock_" #op, \
	  "held lock and object are not in the same allocation\n" \
	  "bpf_spin_lock at off=40 must be held for bpf_list_head" }, \
	{ #test "_global_incorrect_lock_" #op, \
	  "held lock and object are not in the same allocation\n" \
	  "bpf_spin_lock at off=16 must be held for bpf_list_head" }, \
	{ #test "_map_incorrect_lock_" #op, \
	  "held lock and object are not in the same allocation\n" \
	  "bpf_spin_lock at off=0 must be held for bpf_list_head" }, \
	{ #test "_inner_map_incorrect_lock_" #op, \
	  "held lock and object are not in the same allocation\n" \
	  "bpf_spin_lock at off=0 must be held for bpf_list_head" },
	TEST(kptr, push_front)
	TEST(kptr, push_back)
	TEST(kptr, pop_front)
	TEST(kptr, pop_back)
	TEST(global, push_front)
	TEST(global, push_back)
	TEST(global, pop_front)
	TEST(global, pop_back)
	TEST(map, push_front)
	TEST(map, push_back)
	TEST(map, pop_front)
	TEST(map, pop_back)
	TEST(inner_map, push_front)
	TEST(inner_map, push_back)
	TEST(inner_map, pop_front)
	TEST(inner_map, pop_back)
#undef TEST
	{ "map_compat_kprobe", "tracing progs cannot use bpf_{list_head,rb_root} yet" },
	{ "map_compat_kretprobe", "tracing progs cannot use bpf_{list_head,rb_root} yet" },
	{ "map_compat_tp", "tracing progs cannot use bpf_{list_head,rb_root} yet" },
	{ "map_compat_perf", "tracing progs cannot use bpf_{list_head,rb_root} yet" },
	{ "map_compat_raw_tp", "tracing progs cannot use bpf_{list_head,rb_root} yet" },
	{ "map_compat_raw_tp_w", "tracing progs cannot use bpf_{list_head,rb_root} yet" },
	{ "obj_type_id_oor", "local type ID argument must be in range [0, U32_MAX]" },
	{ "obj_new_no_composite", "bpf_obj_new/bpf_percpu_obj_new type ID argument must be of a struct" },
	{ "obj_new_no_struct", "bpf_obj_new/bpf_percpu_obj_new type ID argument must be of a struct" },
	{ "obj_drop_non_zero_off", "R1 must have zero offset when passed to release func" },
	{ "new_null_ret", "R0 invalid mem access 'ptr_or_null_'" },
	{ "obj_new_acq", "Unreleased reference id=" },
	{ "use_after_drop", "invalid mem access 'scalar'" },
	{ "ptr_walk_scalar", "type=rdonly_untrusted_mem expected=percpu_ptr_" },
	{ "direct_read_lock", "direct access to bpf_spin_lock is disallowed" },
	{ "direct_write_lock", "direct access to bpf_spin_lock is disallowed" },
	{ "direct_read_head", "direct access to bpf_list_head is disallowed" },
	{ "direct_write_head", "direct access to bpf_list_head is disallowed" },
	{ "direct_read_node", "direct access to bpf_list_node is disallowed" },
	{ "direct_write_node", "direct access to bpf_list_node is disallowed" },
	{ "use_after_unlock_push_front", "invalid mem access 'scalar'" },
	{ "use_after_unlock_push_back", "invalid mem access 'scalar'" },
	{ "double_push_front", "arg#1 expected pointer to allocated object" },
	{ "double_push_back", "arg#1 expected pointer to allocated object" },
	{ "no_node_value_type", "bpf_list_node not found at offset=0" },
	{ "incorrect_value_type",
	  "operation on bpf_list_head expects arg#1 bpf_list_node at offset=48 in struct foo, "
	  "but arg is at offset=0 in struct bar" },
	{ "incorrect_node_var_off", "variable ptr_ access var_off=(0x0; 0xffffffff) disallowed" },
	{ "incorrect_node_off1", "bpf_list_node not found at offset=49" },
	{ "incorrect_node_off2", "arg#1 offset=0, but expected bpf_list_node at offset=48 in struct foo" },
	{ "no_head_type", "bpf_list_head not found at offset=0" },
	{ "incorrect_head_var_off1", "R1 doesn't have constant offset" },
	{ "incorrect_head_var_off2", "variable ptr_ access var_off=(0x0; 0xffffffff) disallowed" },
	{ "incorrect_head_off1", "bpf_list_head not found at offset=25" },
	{ "incorrect_head_off2", "bpf_list_head not found at offset=1" },
	{ "pop_front_off", "off 48 doesn't point to 'struct bpf_spin_lock' that is at 40" },
	{ "pop_back_off", "off 48 doesn't point to 'struct bpf_spin_lock' that is at 40" },
};

static void test_linked_list_fail_prog(const char *prog_name, const char *err_msg)
{
	LIBBPF_OPTS(bpf_object_open_opts, opts, .kernel_log_buf = log_buf,
						.kernel_log_size = sizeof(log_buf),
						.kernel_log_level = 1);
	struct linked_list_fail *skel;
	struct bpf_program *prog;
	int ret;

	skel = linked_list_fail__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "linked_list_fail__open_opts"))
		return;

	prog = bpf_object__find_program_by_name(skel->obj, prog_name);
	if (!ASSERT_OK_PTR(prog, "bpf_object__find_program_by_name"))
		goto end;

	bpf_program__set_autoload(prog, true);

	ret = linked_list_fail__load(skel);
	if (!ASSERT_ERR(ret, "linked_list_fail__load must fail"))
		goto end;

	if (!ASSERT_OK_PTR(strstr(log_buf, err_msg), "expected error message")) {
		fprintf(stderr, "Expected: %s\n", err_msg);
		fprintf(stderr, "Verifier: %s\n", log_buf);
	}

end:
	linked_list_fail__destroy(skel);
}

static void clear_fields(struct bpf_map *map)
{
	char buf[24];
	int key = 0;

	memset(buf, 0xff, sizeof(buf));
	ASSERT_OK(bpf_map__update_elem(map, &key, sizeof(key), buf, sizeof(buf), 0), "check_and_free_fields");
}

enum {
	TEST_ALL,
	PUSH_POP,
	PUSH_POP_MULT,
	LIST_IN_LIST,
};

static void test_linked_list_success(int mode, bool leave_in_map)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		.data_in = &pkt_v4,
		.data_size_in = sizeof(pkt_v4),
		.repeat = 1,
	);
	struct linked_list *skel;
	int ret;

	skel = linked_list__open_and_load();
	if (!ASSERT_OK_PTR(skel, "linked_list__open_and_load"))
		return;

	if (mode == LIST_IN_LIST)
		goto lil;
	if (mode == PUSH_POP_MULT)
		goto ppm;

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.map_list_push_pop), &opts);
	ASSERT_OK(ret, "map_list_push_pop");
	ASSERT_OK(opts.retval, "map_list_push_pop retval");
	if (!leave_in_map)
		clear_fields(skel->maps.array_map);

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.inner_map_list_push_pop), &opts);
	ASSERT_OK(ret, "inner_map_list_push_pop");
	ASSERT_OK(opts.retval, "inner_map_list_push_pop retval");
	if (!leave_in_map)
		clear_fields(skel->maps.inner_map);

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.global_list_push_pop), &opts);
	ASSERT_OK(ret, "global_list_push_pop");
	ASSERT_OK(opts.retval, "global_list_push_pop retval");
	if (!leave_in_map)
		clear_fields(skel->maps.bss_A);

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.global_list_push_pop_nested), &opts);
	ASSERT_OK(ret, "global_list_push_pop_nested");
	ASSERT_OK(opts.retval, "global_list_push_pop_nested retval");
	if (!leave_in_map)
		clear_fields(skel->maps.bss_A);

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.global_list_array_push_pop), &opts);
	ASSERT_OK(ret, "global_list_array_push_pop");
	ASSERT_OK(opts.retval, "global_list_array_push_pop retval");
	if (!leave_in_map)
		clear_fields(skel->maps.bss_A);

	if (mode == PUSH_POP)
		goto end;

ppm:
	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.map_list_push_pop_multiple), &opts);
	ASSERT_OK(ret, "map_list_push_pop_multiple");
	ASSERT_OK(opts.retval, "map_list_push_pop_multiple retval");
	if (!leave_in_map)
		clear_fields(skel->maps.array_map);

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.inner_map_list_push_pop_multiple), &opts);
	ASSERT_OK(ret, "inner_map_list_push_pop_multiple");
	ASSERT_OK(opts.retval, "inner_map_list_push_pop_multiple retval");
	if (!leave_in_map)
		clear_fields(skel->maps.inner_map);

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.global_list_push_pop_multiple), &opts);
	ASSERT_OK(ret, "global_list_push_pop_multiple");
	ASSERT_OK(opts.retval, "global_list_push_pop_multiple retval");
	if (!leave_in_map)
		clear_fields(skel->maps.bss_A);

	if (mode == PUSH_POP_MULT)
		goto end;

lil:
	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.map_list_in_list), &opts);
	ASSERT_OK(ret, "map_list_in_list");
	ASSERT_OK(opts.retval, "map_list_in_list retval");
	if (!leave_in_map)
		clear_fields(skel->maps.array_map);

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.inner_map_list_in_list), &opts);
	ASSERT_OK(ret, "inner_map_list_in_list");
	ASSERT_OK(opts.retval, "inner_map_list_in_list retval");
	if (!leave_in_map)
		clear_fields(skel->maps.inner_map);

	ret = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.global_list_in_list), &opts);
	ASSERT_OK(ret, "global_list_in_list");
	ASSERT_OK(opts.retval, "global_list_in_list retval");
	if (!leave_in_map)
		clear_fields(skel->maps.bss_A);
end:
	linked_list__destroy(skel);
}

#define SPIN_LOCK 2
#define LIST_HEAD 3
#define LIST_NODE 4

static struct btf *init_btf(void)
{
	int id, lid, hid, nid;
	struct btf *btf;

	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "btf__new_empty"))
		return NULL;
	id = btf__add_int(btf, "int", 4, BTF_INT_SIGNED);
	if (!ASSERT_EQ(id, 1, "btf__add_int"))
		goto end;
	lid = btf__add_struct(btf, "bpf_spin_lock", 4);
	if (!ASSERT_EQ(lid, SPIN_LOCK, "btf__add_struct bpf_spin_lock"))
		goto end;
	hid = btf__add_struct(btf, "bpf_list_head", 16);
	if (!ASSERT_EQ(hid, LIST_HEAD, "btf__add_struct bpf_list_head"))
		goto end;
	nid = btf__add_struct(btf, "bpf_list_node", 24);
	if (!ASSERT_EQ(nid, LIST_NODE, "btf__add_struct bpf_list_node"))
		goto end;
	return btf;
end:
	btf__free(btf);
	return NULL;
}

static void list_and_rb_node_same_struct(bool refcount_field)
{
	int bpf_rb_node_btf_id, bpf_refcount_btf_id = 0, foo_btf_id;
	struct btf *btf;
	int id, err;

	btf = init_btf();
	if (!ASSERT_OK_PTR(btf, "init_btf"))
		return;

	bpf_rb_node_btf_id = btf__add_struct(btf, "bpf_rb_node", 32);
	if (!ASSERT_GT(bpf_rb_node_btf_id, 0, "btf__add_struct bpf_rb_node"))
		return;

	if (refcount_field) {
		bpf_refcount_btf_id = btf__add_struct(btf, "bpf_refcount", 4);
		if (!ASSERT_GT(bpf_refcount_btf_id, 0, "btf__add_struct bpf_refcount"))
			return;
	}

	id = btf__add_struct(btf, "bar", refcount_field ? 60 : 56);
	if (!ASSERT_GT(id, 0, "btf__add_struct bar"))
		return;
	err = btf__add_field(btf, "a", LIST_NODE, 0, 0);
	if (!ASSERT_OK(err, "btf__add_field bar::a"))
		return;
	err = btf__add_field(btf, "c", bpf_rb_node_btf_id, 192, 0);
	if (!ASSERT_OK(err, "btf__add_field bar::c"))
		return;
	if (refcount_field) {
		err = btf__add_field(btf, "ref", bpf_refcount_btf_id, 448, 0);
		if (!ASSERT_OK(err, "btf__add_field bar::ref"))
			return;
	}

	foo_btf_id = btf__add_struct(btf, "foo", 20);
	if (!ASSERT_GT(foo_btf_id, 0, "btf__add_struct foo"))
		return;
	err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
	if (!ASSERT_OK(err, "btf__add_field foo::a"))
		return;
	err = btf__add_field(btf, "b", SPIN_LOCK, 128, 0);
	if (!ASSERT_OK(err, "btf__add_field foo::b"))
		return;
	id = btf__add_decl_tag(btf, "contains:bar:a", foo_btf_id, 0);
	if (!ASSERT_GT(id, 0, "btf__add_decl_tag contains:bar:a"))
		return;

	err = btf__load_into_kernel(btf);
	ASSERT_EQ(err, refcount_field ? 0 : -EINVAL, "check btf");
	btf__free(btf);
}

static void test_btf(void)
{
	struct btf *btf = NULL;
	int id, err;

	while (test__start_subtest("btf: too many locks")) {
		btf = init_btf();
		if (!ASSERT_OK_PTR(btf, "init_btf"))
			break;
		id = btf__add_struct(btf, "foo", 24);
		if (!ASSERT_EQ(id, 5, "btf__add_struct foo"))
			break;
		err = btf__add_field(btf, "a", SPIN_LOCK, 0, 0);
		if (!ASSERT_OK(err, "btf__add_struct foo::a"))
			break;
		err = btf__add_field(btf, "b", SPIN_LOCK, 32, 0);
		if (!ASSERT_OK(err, "btf__add_struct foo::a"))
			break;
		err = btf__add_field(btf, "c", LIST_HEAD, 64, 0);
		if (!ASSERT_OK(err, "btf__add_struct foo::a"))
			break;

		err = btf__load_into_kernel(btf);
		ASSERT_EQ(err, -E2BIG, "check btf");
		btf__free(btf);
		break;
	}

	while (test__start_subtest("btf: missing lock")) {
		btf = init_btf();
		if (!ASSERT_OK_PTR(btf, "init_btf"))
			break;
		id = btf__add_struct(btf, "foo", 16);
		if (!ASSERT_EQ(id, 5, "btf__add_struct foo"))
			break;
		err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
		if (!ASSERT_OK(err, "btf__add_struct foo::a"))
			break;
		id = btf__add_decl_tag(btf, "contains:baz:a", 5, 0);
		if (!ASSERT_EQ(id, 6, "btf__add_decl_tag contains:baz:a"))
			break;
		id = btf__add_struct(btf, "baz", 16);
		if (!ASSERT_EQ(id, 7, "btf__add_struct baz"))
			break;
		err = btf__add_field(btf, "a", LIST_NODE, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field baz::a"))
			break;

		err = btf__load_into_kernel(btf);
		ASSERT_EQ(err, -EINVAL, "check btf");
		btf__free(btf);
		break;
	}

	while (test__start_subtest("btf: bad offset")) {
		btf = init_btf();
		if (!ASSERT_OK_PTR(btf, "init_btf"))
			break;
		id = btf__add_struct(btf, "foo", 36);
		if (!ASSERT_EQ(id, 5, "btf__add_struct foo"))
			break;
		err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::a"))
			break;
		err = btf__add_field(btf, "b", LIST_NODE, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::b"))
			break;
		err = btf__add_field(btf, "c", SPIN_LOCK, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::c"))
			break;
		id = btf__add_decl_tag(btf, "contains:foo:b", 5, 0);
		if (!ASSERT_EQ(id, 6, "btf__add_decl_tag contains:foo:b"))
			break;

		err = btf__load_into_kernel(btf);
		ASSERT_EQ(err, -EEXIST, "check btf");
		btf__free(btf);
		break;
	}

	while (test__start_subtest("btf: missing contains:")) {
		btf = init_btf();
		if (!ASSERT_OK_PTR(btf, "init_btf"))
			break;
		id = btf__add_struct(btf, "foo", 24);
		if (!ASSERT_EQ(id, 5, "btf__add_struct foo"))
			break;
		err = btf__add_field(btf, "a", SPIN_LOCK, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::a"))
			break;
		err = btf__add_field(btf, "b", LIST_HEAD, 64, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::b"))
			break;

		err = btf__load_into_kernel(btf);
		ASSERT_EQ(err, -EINVAL, "check btf");
		btf__free(btf);
		break;
	}

	while (test__start_subtest("btf: missing struct")) {
		btf = init_btf();
		if (!ASSERT_OK_PTR(btf, "init_btf"))
			break;
		id = btf__add_struct(btf, "foo", 24);
		if (!ASSERT_EQ(id, 5, "btf__add_struct foo"))
			break;
		err = btf__add_field(btf, "a", SPIN_LOCK, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::a"))
			break;
		err = btf__add_field(btf, "b", LIST_HEAD, 64, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::b"))
			break;
		id = btf__add_decl_tag(btf, "contains:bar:bar", 5, 1);
		if (!ASSERT_EQ(id, 6, "btf__add_decl_tag contains:bar:bar"))
			break;

		err = btf__load_into_kernel(btf);
		ASSERT_EQ(err, -ENOENT, "check btf");
		btf__free(btf);
		break;
	}

	while (test__start_subtest("btf: missing node")) {
		btf = init_btf();
		if (!ASSERT_OK_PTR(btf, "init_btf"))
			break;
		id = btf__add_struct(btf, "foo", 24);
		if (!ASSERT_EQ(id, 5, "btf__add_struct foo"))
			break;
		err = btf__add_field(btf, "a", SPIN_LOCK, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::a"))
			break;
		err = btf__add_field(btf, "b", LIST_HEAD, 64, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::b"))
			break;
		id = btf__add_decl_tag(btf, "contains:foo:c", 5, 1);
		if (!ASSERT_EQ(id, 6, "btf__add_decl_tag contains:foo:c"))
			break;

		err = btf__load_into_kernel(btf);
		btf__free(btf);
		ASSERT_EQ(err, -ENOENT, "check btf");
		break;
	}

	while (test__start_subtest("btf: node incorrect type")) {
		btf = init_btf();
		if (!ASSERT_OK_PTR(btf, "init_btf"))
			break;
		id = btf__add_struct(btf, "foo", 20);
		if (!ASSERT_EQ(id, 5, "btf__add_struct foo"))
			break;
		err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::a"))
			break;
		err = btf__add_field(btf, "b", SPIN_LOCK, 128, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::b"))
			break;
		id = btf__add_decl_tag(btf, "contains:bar:a", 5, 0);
		if (!ASSERT_EQ(id, 6, "btf__add_decl_tag contains:bar:a"))
			break;
		id = btf__add_struct(btf, "bar", 4);
		if (!ASSERT_EQ(id, 7, "btf__add_struct bar"))
			break;
		err = btf__add_field(btf, "a", SPIN_LOCK, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field bar::a"))
			break;

		err = btf__load_into_kernel(btf);
		ASSERT_EQ(err, -EINVAL, "check btf");
		btf__free(btf);
		break;
	}

	while (test__start_subtest("btf: multiple bpf_list_node with name b")) {
		btf = init_btf();
		if (!ASSERT_OK_PTR(btf, "init_btf"))
			break;
		id = btf__add_struct(btf, "foo", 52);
		if (!ASSERT_EQ(id, 5, "btf__add_struct foo"))
			break;
		err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::a"))
			break;
		err = btf__add_field(btf, "b", LIST_NODE, 128, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::b"))
			break;
		err = btf__add_field(btf, "b", LIST_NODE, 256, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::c"))
			break;
		err = btf__add_field(btf, "d", SPIN_LOCK, 384, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::d"))
			break;
		id = btf__add_decl_tag(btf, "contains:foo:b", 5, 0);
		if (!ASSERT_EQ(id, 6, "btf__add_decl_tag contains:foo:b"))
			break;

		err = btf__load_into_kernel(btf);
		ASSERT_EQ(err, -EINVAL, "check btf");
		btf__free(btf);
		break;
	}

	while (test__start_subtest("btf: owning | owned AA cycle")) {
		btf = init_btf();
		if (!ASSERT_OK_PTR(btf, "init_btf"))
			break;
		id = btf__add_struct(btf, "foo", 44);
		if (!ASSERT_EQ(id, 5, "btf__add_struct foo"))
			break;
		err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::a"))
			break;
		err = btf__add_field(btf, "b", LIST_NODE, 128, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::b"))
			break;
		err = btf__add_field(btf, "c", SPIN_LOCK, 320, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::c"))
			break;
		id = btf__add_decl_tag(btf, "contains:foo:b", 5, 0);
		if (!ASSERT_EQ(id, 6, "btf__add_decl_tag contains:foo:b"))
			break;

		err = btf__load_into_kernel(btf);
		ASSERT_EQ(err, -ELOOP, "check btf");
		btf__free(btf);
		break;
	}

	while (test__start_subtest("btf: owning | owned ABA cycle")) {
		btf = init_btf();
		if (!ASSERT_OK_PTR(btf, "init_btf"))
			break;
		id = btf__add_struct(btf, "foo", 44);
		if (!ASSERT_EQ(id, 5, "btf__add_struct foo"))
			break;
		err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::a"))
			break;
		err = btf__add_field(btf, "b", LIST_NODE, 128, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::b"))
			break;
		err = btf__add_field(btf, "c", SPIN_LOCK, 320, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::c"))
			break;
		id = btf__add_decl_tag(btf, "contains:bar:b", 5, 0);
		if (!ASSERT_EQ(id, 6, "btf__add_decl_tag contains:bar:b"))
			break;
		id = btf__add_struct(btf, "bar", 44);
		if (!ASSERT_EQ(id, 7, "btf__add_struct bar"))
			break;
		err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field bar::a"))
			break;
		err = btf__add_field(btf, "b", LIST_NODE, 128, 0);
		if (!ASSERT_OK(err, "btf__add_field bar::b"))
			break;
		err = btf__add_field(btf, "c", SPIN_LOCK, 320, 0);
		if (!ASSERT_OK(err, "btf__add_field bar::c"))
			break;
		id = btf__add_decl_tag(btf, "contains:foo:b", 7, 0);
		if (!ASSERT_EQ(id, 8, "btf__add_decl_tag contains:foo:b"))
			break;

		err = btf__load_into_kernel(btf);
		ASSERT_EQ(err, -ELOOP, "check btf");
		btf__free(btf);
		break;
	}

	while (test__start_subtest("btf: owning -> owned")) {
		btf = init_btf();
		if (!ASSERT_OK_PTR(btf, "init_btf"))
			break;
		id = btf__add_struct(btf, "foo", 28);
		if (!ASSERT_EQ(id, 5, "btf__add_struct foo"))
			break;
		err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::a"))
			break;
		err = btf__add_field(btf, "b", SPIN_LOCK, 192, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::b"))
			break;
		id = btf__add_decl_tag(btf, "contains:bar:a", 5, 0);
		if (!ASSERT_EQ(id, 6, "btf__add_decl_tag contains:bar:a"))
			break;
		id = btf__add_struct(btf, "bar", 24);
		if (!ASSERT_EQ(id, 7, "btf__add_struct bar"))
			break;
		err = btf__add_field(btf, "a", LIST_NODE, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field bar::a"))
			break;

		err = btf__load_into_kernel(btf);
		ASSERT_EQ(err, 0, "check btf");
		btf__free(btf);
		break;
	}

	while (test__start_subtest("btf: owning -> owning | owned -> owned")) {
		btf = init_btf();
		if (!ASSERT_OK_PTR(btf, "init_btf"))
			break;
		id = btf__add_struct(btf, "foo", 28);
		if (!ASSERT_EQ(id, 5, "btf__add_struct foo"))
			break;
		err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::a"))
			break;
		err = btf__add_field(btf, "b", SPIN_LOCK, 192, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::b"))
			break;
		id = btf__add_decl_tag(btf, "contains:bar:b", 5, 0);
		if (!ASSERT_EQ(id, 6, "btf__add_decl_tag contains:bar:b"))
			break;
		id = btf__add_struct(btf, "bar", 44);
		if (!ASSERT_EQ(id, 7, "btf__add_struct bar"))
			break;
		err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field bar::a"))
			break;
		err = btf__add_field(btf, "b", LIST_NODE, 128, 0);
		if (!ASSERT_OK(err, "btf__add_field bar::b"))
			break;
		err = btf__add_field(btf, "c", SPIN_LOCK, 320, 0);
		if (!ASSERT_OK(err, "btf__add_field bar::c"))
			break;
		id = btf__add_decl_tag(btf, "contains:baz:a", 7, 0);
		if (!ASSERT_EQ(id, 8, "btf__add_decl_tag contains:baz:a"))
			break;
		id = btf__add_struct(btf, "baz", 24);
		if (!ASSERT_EQ(id, 9, "btf__add_struct baz"))
			break;
		err = btf__add_field(btf, "a", LIST_NODE, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field baz:a"))
			break;

		err = btf__load_into_kernel(btf);
		ASSERT_EQ(err, 0, "check btf");
		btf__free(btf);
		break;
	}

	while (test__start_subtest("btf: owning | owned -> owning | owned -> owned")) {
		btf = init_btf();
		if (!ASSERT_OK_PTR(btf, "init_btf"))
			break;
		id = btf__add_struct(btf, "foo", 44);
		if (!ASSERT_EQ(id, 5, "btf__add_struct foo"))
			break;
		err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::a"))
			break;
		err = btf__add_field(btf, "b", LIST_NODE, 128, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::b"))
			break;
		err = btf__add_field(btf, "c", SPIN_LOCK, 320, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::c"))
			break;
		id = btf__add_decl_tag(btf, "contains:bar:b", 5, 0);
		if (!ASSERT_EQ(id, 6, "btf__add_decl_tag contains:bar:b"))
			break;
		id = btf__add_struct(btf, "bar", 44);
		if (!ASSERT_EQ(id, 7, "btf__add_struct bar"))
			break;
		err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field bar:a"))
			break;
		err = btf__add_field(btf, "b", LIST_NODE, 128, 0);
		if (!ASSERT_OK(err, "btf__add_field bar:b"))
			break;
		err = btf__add_field(btf, "c", SPIN_LOCK, 320, 0);
		if (!ASSERT_OK(err, "btf__add_field bar:c"))
			break;
		id = btf__add_decl_tag(btf, "contains:baz:a", 7, 0);
		if (!ASSERT_EQ(id, 8, "btf__add_decl_tag contains:baz:a"))
			break;
		id = btf__add_struct(btf, "baz", 24);
		if (!ASSERT_EQ(id, 9, "btf__add_struct baz"))
			break;
		err = btf__add_field(btf, "a", LIST_NODE, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field baz:a"))
			break;

		err = btf__load_into_kernel(btf);
		ASSERT_EQ(err, -ELOOP, "check btf");
		btf__free(btf);
		break;
	}

	while (test__start_subtest("btf: owning -> owning | owned -> owning | owned -> owned")) {
		btf = init_btf();
		if (!ASSERT_OK_PTR(btf, "init_btf"))
			break;
		id = btf__add_struct(btf, "foo", 20);
		if (!ASSERT_EQ(id, 5, "btf__add_struct foo"))
			break;
		err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::a"))
			break;
		err = btf__add_field(btf, "b", SPIN_LOCK, 128, 0);
		if (!ASSERT_OK(err, "btf__add_field foo::b"))
			break;
		id = btf__add_decl_tag(btf, "contains:bar:b", 5, 0);
		if (!ASSERT_EQ(id, 6, "btf__add_decl_tag contains:bar:b"))
			break;
		id = btf__add_struct(btf, "bar", 44);
		if (!ASSERT_EQ(id, 7, "btf__add_struct bar"))
			break;
		err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field bar::a"))
			break;
		err = btf__add_field(btf, "b", LIST_NODE, 128, 0);
		if (!ASSERT_OK(err, "btf__add_field bar::b"))
			break;
		err = btf__add_field(btf, "c", SPIN_LOCK, 320, 0);
		if (!ASSERT_OK(err, "btf__add_field bar::c"))
			break;
		id = btf__add_decl_tag(btf, "contains:baz:b", 7, 0);
		if (!ASSERT_EQ(id, 8, "btf__add_decl_tag"))
			break;
		id = btf__add_struct(btf, "baz", 44);
		if (!ASSERT_EQ(id, 9, "btf__add_struct baz"))
			break;
		err = btf__add_field(btf, "a", LIST_HEAD, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field bar::a"))
			break;
		err = btf__add_field(btf, "b", LIST_NODE, 128, 0);
		if (!ASSERT_OK(err, "btf__add_field bar::b"))
			break;
		err = btf__add_field(btf, "c", SPIN_LOCK, 320, 0);
		if (!ASSERT_OK(err, "btf__add_field bar::c"))
			break;
		id = btf__add_decl_tag(btf, "contains:bam:a", 9, 0);
		if (!ASSERT_EQ(id, 10, "btf__add_decl_tag contains:bam:a"))
			break;
		id = btf__add_struct(btf, "bam", 24);
		if (!ASSERT_EQ(id, 11, "btf__add_struct bam"))
			break;
		err = btf__add_field(btf, "a", LIST_NODE, 0, 0);
		if (!ASSERT_OK(err, "btf__add_field bam::a"))
			break;

		err = btf__load_into_kernel(btf);
		ASSERT_EQ(err, -ELOOP, "check btf");
		btf__free(btf);
		break;
	}

	while (test__start_subtest("btf: list_node and rb_node in same struct")) {
		list_and_rb_node_same_struct(true);
		break;
	}

	while (test__start_subtest("btf: list_node and rb_node in same struct, no bpf_refcount")) {
		list_and_rb_node_same_struct(false);
		break;
	}
}

void test_linked_list(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(linked_list_fail_tests); i++) {
		if (!test__start_subtest(linked_list_fail_tests[i].prog_name))
			continue;
		test_linked_list_fail_prog(linked_list_fail_tests[i].prog_name,
					   linked_list_fail_tests[i].err_msg);
	}
	test_btf();
	test_linked_list_success(PUSH_POP, false);
	test_linked_list_success(PUSH_POP, true);
	test_linked_list_success(PUSH_POP_MULT, false);
	test_linked_list_success(PUSH_POP_MULT, true);
	test_linked_list_success(LIST_IN_LIST, false);
	test_linked_list_success(LIST_IN_LIST, true);
	test_linked_list_success(TEST_ALL, false);
}

void test_linked_list_peek(void)
{
	RUN_TESTS(linked_list_peek);
}
