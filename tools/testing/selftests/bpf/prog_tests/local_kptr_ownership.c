// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <bpf/btf.h>
#include <linux/btf.h>
#include <test_progs.h>

#define SPIN_LOCK 2
#define LIST_HEAD 3
#define LIST_NODE 4
/* Keep in sync with BTF_MAX_OWNERSHIP_DEPTH. */
#define MAX_OWNERSHIP_DEPTH 8

static struct btf *init_btf(void)
{
	struct btf *btf;
	int id;

	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "btf__new_empty"))
		return NULL;
	id = btf__add_int(btf, "int", 4, BTF_INT_SIGNED);
	if (!ASSERT_EQ(id, 1, "btf__add_int"))
		goto err_out;
	id = btf__add_struct(btf, "bpf_spin_lock", 4);
	if (!ASSERT_EQ(id, SPIN_LOCK, "btf__add_struct bpf_spin_lock"))
		goto err_out;
	id = btf__add_struct(btf, "bpf_list_head", 16);
	if (!ASSERT_EQ(id, LIST_HEAD, "btf__add_struct bpf_list_head"))
		goto err_out;
	id = btf__add_struct(btf, "bpf_list_node", 24);
	if (!ASSERT_EQ(id, LIST_NODE, "btf__add_struct bpf_list_node"))
		goto err_out;
	return btf;

err_out:
	btf__free(btf);
	return NULL;
}

static int add_local_kptr(struct btf *btf, int pointee_id, const char *tag)
{
	int id;

	id = btf__add_type_tag(btf, tag, pointee_id);
	if (!ASSERT_GT(id, 0, "btf__add_type_tag"))
		return id;
	id = btf__add_ptr(btf, id);
	ASSERT_GT(id, 0, "btf__add_ptr");
	return id;
}

static void test_self_cycle(const char *tag, int expected_err)
{
	struct btf *btf;
	int id, err;

	btf = init_btf();
	if (!ASSERT_OK_PTR(btf, "init_btf"))
		return;
	id = add_local_kptr(btf, 7, tag);
	if (id <= 0)
		goto out;
	id = btf__add_struct(btf, "self_cycle", 8);
	if (!ASSERT_EQ(id, 7, "btf__add_struct self_cycle"))
		goto out;
	err = btf__add_field(btf, "next", 6, 0, 0);
	if (!ASSERT_OK(err, "btf__add_field self_cycle::next"))
		goto out;

	err = btf__load_into_kernel(btf);
	ASSERT_EQ(err, expected_err, "check btf");
out:
	btf__free(btf);
}

static void test_aba_cycle(void)
{
	struct btf *btf;
	int id, err;

	btf = init_btf();
	if (!ASSERT_OK_PTR(btf, "init_btf"))
		return;
	id = add_local_kptr(btf, 10, "kptr");
	if (id <= 0)
		goto out;
	id = add_local_kptr(btf, 9, "kptr");
	if (id <= 0)
		goto out;
	id = btf__add_struct(btf, "cycle_a", 8);
	if (!ASSERT_EQ(id, 9, "btf__add_struct cycle_a"))
		goto out;
	err = btf__add_field(btf, "b", 6, 0, 0);
	if (!ASSERT_OK(err, "btf__add_field cycle_a::b"))
		goto out;
	id = btf__add_struct(btf, "cycle_b", 8);
	if (!ASSERT_EQ(id, 10, "btf__add_struct cycle_b"))
		goto out;
	err = btf__add_field(btf, "a", 8, 0, 0);
	if (!ASSERT_OK(err, "btf__add_field cycle_b::a"))
		goto out;

	err = btf__load_into_kernel(btf);
	ASSERT_EQ(err, -ELOOP, "check btf");
out:
	btf__free(btf);
}

static void test_mixed_cycle(void)
{
	struct btf *btf;
	int id, err;

	btf = init_btf();
	if (!ASSERT_OK_PTR(btf, "init_btf"))
		return;
	id = add_local_kptr(btf, 7, "kptr");
	if (id <= 0)
		goto out;
	id = btf__add_struct(btf, "mixed_owner", 20);
	if (!ASSERT_EQ(id, 7, "btf__add_struct mixed_owner"))
		goto out;
	err = btf__add_field(btf, "root", LIST_HEAD, 0, 0);
	if (!ASSERT_OK(err, "btf__add_field mixed_owner::root"))
		goto out;
	err = btf__add_field(btf, "lock", SPIN_LOCK, 128, 0);
	if (!ASSERT_OK(err, "btf__add_field mixed_owner::lock"))
		goto out;
	id = btf__add_decl_tag(btf, "contains:mixed_node:node", 7, 0);
	if (!ASSERT_EQ(id, 8, "btf__add_decl_tag mixed_owner"))
		goto out;
	id = btf__add_struct(btf, "mixed_node", 32);
	if (!ASSERT_EQ(id, 9, "btf__add_struct mixed_node"))
		goto out;
	err = btf__add_field(btf, "node", LIST_NODE, 0, 0);
	if (!ASSERT_OK(err, "btf__add_field mixed_node::node"))
		goto out;
	err = btf__add_field(btf, "owner", 6, 192, 0);
	if (!ASSERT_OK(err, "btf__add_field mixed_node::owner"))
		goto out;

	err = btf__load_into_kernel(btf);
	ASSERT_EQ(err, -ELOOP, "check btf");
out:
	btf__free(btf);
}

static void test_acyclic_depth(int depth, bool child_first, bool shared_suffix, int expected_err)
{
	int ptr_id[MAX_OWNERSHIP_DEPTH + 1];
	int first_struct_id;
	struct btf *btf;
	int id, err, i, n, pointee_id;

	btf = init_btf();
	if (!ASSERT_OK_PTR(btf, "init_btf"))
		return;
	first_struct_id = 5 + 2 * depth;
	for (i = 0; i < depth; i++) {
		if (i == depth - 1)
			pointee_id = first_struct_id + depth;
		else
			pointee_id = first_struct_id + (child_first ? depth - 2 - i : i + 1);
		ptr_id[i] = add_local_kptr(btf, pointee_id, "kptr");
		if (ptr_id[i] <= 0)
			goto out;
	}
	for (n = 0; n < depth; n++) {
		char name[32];
		int offset = 0;

		i = child_first ? depth - 1 - n : n;
		snprintf(name, sizeof(name), "owner_%d", i);
		id = btf__add_struct(btf, name, shared_suffix && !i ? 16 : 8);
		if (!ASSERT_EQ(id, first_struct_id + n, "btf__add_struct owner"))
			goto out;
		if (shared_suffix && !i) {
			/*
			 * Visit the shared suffix through the shorter path before
			 * reaching it again with less remaining depth.
			 */
			err = btf__add_field(btf, "suffix", ptr_id[1], 0, 0);
			if (!ASSERT_OK(err, "btf__add_field owner::suffix"))
				goto out;
			offset = 64;
		}
		err = btf__add_field(btf, "next", ptr_id[i], offset, 0);
		if (!ASSERT_OK(err, "btf__add_field owner::next"))
			goto out;
	}
	id = btf__add_struct(btf, "plain_leaf", 4);
	if (!ASSERT_EQ(id, first_struct_id + depth, "btf__add_struct plain_leaf"))
		goto out;

	err = btf__load_into_kernel(btf);
	ASSERT_EQ(err, expected_err, "check btf");
out:
	btf__free(btf);
}

static void test_graph_depth(bool rbtree, int depth, int expected_err)
{
	int root_type = LIST_HEAD, node_type = LIST_NODE, node_size = 24;
	int id, err, i, lock_off, root_off, size;
	struct btf *btf;

	btf = init_btf();
	if (!ASSERT_OK_PTR(btf, "init_btf"))
		return;
	if (rbtree) {
		root_type = btf__add_struct(btf, "bpf_rb_root", 16);
		if (!ASSERT_GT(root_type, 0, "btf__add_struct bpf_rb_root"))
			goto out;
		node_type = btf__add_struct(btf, "bpf_rb_node", 32);
		if (!ASSERT_GT(node_type, 0, "btf__add_struct bpf_rb_node"))
			goto out;
		node_size = 32;
	}

	for (i = 0; i < depth; i++) {
		char name[32], tag[64];

		lock_off = i ? node_size : 0;
		root_off = lock_off + 8;
		size = i == depth - 1 ? node_size : root_off + 16;
		snprintf(name, sizeof(name), "graph_owner_%d", i);
		id = btf__add_struct(btf, name, size);
		if (!ASSERT_GT(id, 0, "btf__add_struct graph_owner"))
			goto out;
		if (i) {
			err = btf__add_field(btf, "node", node_type, 0, 0);
			if (!ASSERT_OK(err, "btf__add_field graph_owner::node"))
				goto out;
		}
		if (i == depth - 1)
			continue;
		err = btf__add_field(btf, "lock", SPIN_LOCK, lock_off * 8, 0);
		if (!ASSERT_OK(err, "btf__add_field graph_owner::lock"))
			goto out;
		err = btf__add_field(btf, "root", root_type, root_off * 8, 0);
		if (!ASSERT_OK(err, "btf__add_field graph_owner::root"))
			goto out;
		snprintf(tag, sizeof(tag), "contains:graph_owner_%d:node", i + 1);
		err = btf__add_decl_tag(btf, tag, id, i ? 2 : 1);
		if (!ASSERT_GT(err, 0, "btf__add_decl_tag graph_owner"))
			goto out;
	}

	err = btf__load_into_kernel(btf);
	ASSERT_EQ(err, expected_err, "check btf");
out:
	btf__free(btf);
}

void test_local_kptr_ownership(void)
{
	if (test__start_subtest("self_cycle"))
		test_self_cycle("kptr", -ELOOP);
	if (test__start_subtest("untrusted_self_cycle"))
		test_self_cycle("kptr_untrusted", 0);
	if (test__start_subtest("percpu_self_cycle"))
		test_self_cycle("percpu_kptr", -ELOOP);
	if (test__start_subtest("ABA_cycle"))
		test_aba_cycle();
	if (test__start_subtest("mixed_graph_root_cycle"))
		test_mixed_cycle();
	if (test__start_subtest("max_acyclic"))
		test_acyclic_depth(MAX_OWNERSHIP_DEPTH, false, false, 0);
	if (test__start_subtest("too_deep_acyclic"))
		test_acyclic_depth(MAX_OWNERSHIP_DEPTH + 1, false, false, -ELOOP);
	if (test__start_subtest("max_acyclic_child_first"))
		test_acyclic_depth(MAX_OWNERSHIP_DEPTH, true, false, 0);
	if (test__start_subtest("too_deep_acyclic_child_first"))
		test_acyclic_depth(MAX_OWNERSHIP_DEPTH + 1, true, false, -ELOOP);
	if (test__start_subtest("max_acyclic_shared_suffix"))
		test_acyclic_depth(MAX_OWNERSHIP_DEPTH, false, true, 0);
	if (test__start_subtest("too_deep_acyclic_shared_suffix"))
		test_acyclic_depth(MAX_OWNERSHIP_DEPTH + 1, false, true, -ELOOP);
	if (test__start_subtest("list_three_types"))
		test_graph_depth(false, 3, 0);
	if (test__start_subtest("list_four_types"))
		test_graph_depth(false, 4, 0);
	if (test__start_subtest("list_max_depth"))
		test_graph_depth(false, MAX_OWNERSHIP_DEPTH, 0);
	if (test__start_subtest("list_too_deep"))
		test_graph_depth(false, MAX_OWNERSHIP_DEPTH + 1, -ELOOP);
	if (test__start_subtest("rbtree_three_types"))
		test_graph_depth(true, 3, 0);
	if (test__start_subtest("rbtree_four_types"))
		test_graph_depth(true, 4, 0);
	if (test__start_subtest("rbtree_max_depth"))
		test_graph_depth(true, MAX_OWNERSHIP_DEPTH, 0);
	if (test__start_subtest("rbtree_too_deep"))
		test_graph_depth(true, MAX_OWNERSHIP_DEPTH + 1, -ELOOP);
}
