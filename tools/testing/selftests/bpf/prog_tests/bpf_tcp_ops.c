// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <test_progs.h>
#include <network_helpers.h>
#include <bpf/btf.h>
#include "cgroup_helpers.h"
#include "bpf_tcp_ops.skel.h"

#define CGROUP_PATH	"/bpf_tcp_ops"
#define TEST_NETNS	"bpf_tcp_ops"

static __s32 get_bpf_tcp_ops_type_id(void)
{
	struct btf *vmlinux_btf;
	__s32 type_id;

	vmlinux_btf = btf__load_vmlinux_btf();
	if (!ASSERT_OK_PTR(vmlinux_btf, "load_vmlinux_btf"))
		return -1;

	type_id = btf__find_by_name_kind(vmlinux_btf, "bpf_tcp_ops", BTF_KIND_STRUCT);
	btf__free(vmlinux_btf);

	ASSERT_GT(type_id, 0, "find_bpf_tcp_ops");
	return type_id;
}

static void reset_order(struct bpf_tcp_ops *skel)
{
	memset(skel->bss->listen_order, 0, sizeof(skel->bss->listen_order));
	memset(skel->bss->connect_order, 0, sizeof(skel->bss->connect_order));
	skel->bss->listen_cnt = 0;
	skel->bss->connect_cnt = 0;
}

static void do_listen_connect(int family)
{
	const char *addr = family == AF_INET ? "127.0.0.1" : "::1";
	int server_fd, client_fd;

	server_fd = start_server(family, SOCK_STREAM, addr, 0, 0);
	if (!ASSERT_GE(server_fd, 0, "start_server"))
		return;

	client_fd = connect_to_fd(server_fd, 0);
	if (ASSERT_OK_FD(client_fd, "connect_to_fd"))
		close(client_fd);

	close(server_fd);
}

/*
 * Attach ops1 and ops2 normally (in that order), then ops3 with
 * BPF_F_PREORDER. Expected execution order: [3, 1, 2] — ops3 runs
 * first despite being attached last, ops1 before ops2 by attach order.
 */
static void test_order(int cgroup_fd, struct bpf_tcp_ops *skel, int family)
{
	LIBBPF_OPTS(bpf_cgroup_opts, preorder_opts, .flags = BPF_F_PREORDER);
	struct bpf_link *link1 = NULL, *link2 = NULL, *link3 = NULL;

	link1 = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops1, cgroup_fd, NULL);
	if (!ASSERT_OK_PTR(link1, "attach_ops1"))
		goto done;

	link2 = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops2, cgroup_fd, NULL);
	if (!ASSERT_OK_PTR(link2, "attach_ops2"))
		goto done;

	link3 = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops3, cgroup_fd,
					    &preorder_opts);
	if (!ASSERT_OK_PTR(link3, "attach_ops3_preorder"))
		goto done;

	reset_order(skel);
	do_listen_connect(family);

	ASSERT_EQ(skel->bss->listen_cnt, 3, "listen_cnt");
	ASSERT_EQ(skel->bss->listen_order[0], 3, "listen_order[0]");
	ASSERT_EQ(skel->bss->listen_order[1], 1, "listen_order[1]");
	ASSERT_EQ(skel->bss->listen_order[2], 2, "listen_order[2]");

	ASSERT_EQ(skel->bss->connect_cnt, 3, "connect_cnt");
	ASSERT_EQ(skel->bss->connect_order[0], 3, "connect_order[0]");
	ASSERT_EQ(skel->bss->connect_order[1], 1, "connect_order[1]");
	ASSERT_EQ(skel->bss->connect_order[2], 2, "connect_order[2]");

done:
	bpf_link__destroy(link3);
	bpf_link__destroy(link2);
	bpf_link__destroy(link1);
}

static void run_order_subtest(void)
{
	struct bpf_tcp_ops *skel = NULL;
	struct netns_obj *ns = NULL;
	int cgroup_fd;

	cgroup_fd = test__join_cgroup(CGROUP_PATH);
	if (!ASSERT_GE(cgroup_fd, 0, "join_cgroup"))
		return;

	ns = netns_new(TEST_NETNS, true);
	if (!ASSERT_OK_PTR(ns, "netns_new"))
		goto done;

	skel = bpf_tcp_ops__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		goto done;

	test_order(cgroup_fd, skel, AF_INET);
	test_order(cgroup_fd, skel, AF_INET6);

done:
	bpf_tcp_ops__destroy(skel);
	netns_free(ns);
	close(cgroup_fd);
}

/*
 * Position a new attachment relative to an existing one. Attach ops1, then
 * ops2 with BPF_F_BEFORE ops1, then ops3 with BPF_F_AFTER ops2. Expected
 * execution order: [2, 3, 1]. For struct_ops, relative_fd refers to a link
 * fd, so BPF_F_LINK must be set.
 */
static void test_before_after(int cgroup_fd, struct bpf_tcp_ops *skel)
{
	LIBBPF_OPTS(bpf_cgroup_opts, opts);
	struct bpf_link *link1 = NULL, *link2 = NULL, *link3 = NULL;

	link1 = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops1, cgroup_fd, NULL);
	if (!ASSERT_OK_PTR(link1, "attach_ops1"))
		goto done;

	opts.flags = BPF_F_BEFORE | BPF_F_LINK;
	opts.relative_fd = bpf_link__fd(link1);
	link2 = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops2, cgroup_fd, &opts);
	if (!ASSERT_OK_PTR(link2, "attach_ops2_before"))
		goto done;

	opts.flags = BPF_F_AFTER | BPF_F_LINK;
	opts.relative_fd = bpf_link__fd(link2);
	link3 = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops3, cgroup_fd, &opts);
	if (!ASSERT_OK_PTR(link3, "attach_ops3_after"))
		goto done;

	reset_order(skel);
	do_listen_connect(AF_INET6);

	ASSERT_EQ(skel->bss->listen_cnt, 3, "listen_cnt");
	ASSERT_EQ(skel->bss->listen_order[0], 2, "listen_order[0]");
	ASSERT_EQ(skel->bss->listen_order[1], 3, "listen_order[1]");
	ASSERT_EQ(skel->bss->listen_order[2], 1, "listen_order[2]");

done:
	bpf_link__destroy(link3);
	bpf_link__destroy(link2);
	bpf_link__destroy(link1);
}

static void run_before_after_subtest(void)
{
	struct bpf_tcp_ops *skel = NULL;
	struct netns_obj *ns = NULL;
	int cgroup_fd;

	cgroup_fd = test__join_cgroup(CGROUP_PATH);
	if (!ASSERT_GE(cgroup_fd, 0, "join_cgroup"))
		return;

	ns = netns_new(TEST_NETNS, true);
	if (!ASSERT_OK_PTR(ns, "netns_new"))
		goto done;

	skel = bpf_tcp_ops__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		goto done;

	test_before_after(cgroup_fd, skel);

done:
	bpf_tcp_ops__destroy(skel);
	netns_free(ns);
	close(cgroup_fd);
}

static void test_query(int cgroup_fd, struct bpf_tcp_ops *skel)
{
	LIBBPF_OPTS(bpf_prog_query_opts, query_opts);
	struct bpf_map_info info = {};
	__u32 info_len = sizeof(info);
	struct bpf_link *link1 = NULL, *link2 = NULL;
	__u32 map1_id, map2_id, map_ids[2] = {};
	__s32 type_id;

	type_id = get_bpf_tcp_ops_type_id();
	if (type_id <= 0)
		return;

	bpf_map_get_info_by_fd(bpf_map__fd(skel->maps.tcp_ops1), &info, &info_len);
	map1_id = info.id;

	bpf_map_get_info_by_fd(bpf_map__fd(skel->maps.tcp_ops2), &info, &info_len);
	map2_id = info.id;

	link1 = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops1, cgroup_fd, NULL);
	if (!ASSERT_OK_PTR(link1, "attach_ops1"))
		goto done;

	link2 = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops2, cgroup_fd, NULL);
	if (!ASSERT_OK_PTR(link2, "attach_ops2"))
		goto done;

	/* query effective: expect 2 entries in attachment order */
	query_opts.type_id = type_id;
	query_opts.prog_ids = map_ids;
	query_opts.count = ARRAY_SIZE(map_ids);
	query_opts.query_flags = BPF_F_QUERY_EFFECTIVE;
	ASSERT_OK(bpf_prog_query_opts(cgroup_fd, BPF_STRUCT_OPS, &query_opts),
		  "query_effective");
	ASSERT_EQ(query_opts.count, 2, "query_effective_count");
	ASSERT_EQ(map_ids[0], map1_id, "map_ids[0]");
	ASSERT_EQ(map_ids[1], map2_id, "map_ids[1]");

	/* query attached (non-effective): expect 2 entries */
	memset(map_ids, 0, sizeof(map_ids));
	query_opts.query_flags = 0;
	query_opts.count = ARRAY_SIZE(map_ids);
	ASSERT_OK(bpf_prog_query_opts(cgroup_fd, BPF_STRUCT_OPS, &query_opts),
		  "query_attached");
	ASSERT_EQ(query_opts.count, 2, "query_attached_count");
	ASSERT_EQ(map_ids[0], map1_id, "attached_map_ids[0]");
	ASSERT_EQ(map_ids[1], map2_id, "attached_map_ids[1]");

done:
	bpf_link__destroy(link2);
	bpf_link__destroy(link1);
}

static void run_query_subtest(void)
{
	struct bpf_tcp_ops *skel = NULL;
	struct netns_obj *ns = NULL;
	int cgroup_fd;

	cgroup_fd = test__join_cgroup(CGROUP_PATH);
	if (!ASSERT_GE(cgroup_fd, 0, "join_cgroup"))
		return;

	ns = netns_new(TEST_NETNS, true);
	if (!ASSERT_OK_PTR(ns, "netns_new"))
		goto done;

	skel = bpf_tcp_ops__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		goto done;

	test_query(cgroup_fd, skel);

done:
	bpf_tcp_ops__destroy(skel);
	netns_free(ns);
	close(cgroup_fd);
}

/* Must match progs/bpf_tcp_ops.c */
#define OPS_RETVAL1	11
#define OPS_RETVAL2	22

/*
 * Attach three struct_ops implementing timeout_init to the same cgroup; they
 * run in attach order [retval1, retval2, retval3]. timeout_init's return value
 * is chained: the first prog reads the kernel seed via bpf_get_retval() (0,
 * since no legacy sockops prog is attached) and returns OPS_RETVAL1; each
 * subsequent prog must then observe the previous prog's return value. This
 * proves the trampoline inherits the retval across an array of struct_ops.
 */
static void test_retval(int cgroup_fd, struct bpf_tcp_ops *skel)
{
	struct bpf_link *link1 = NULL, *link2 = NULL, *link3 = NULL;

	skel->bss->retval_saw1 = -1;
	skel->bss->retval_saw2 = -1;
	skel->bss->retval_saw3 = -1;

	link1 = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops_retval1, cgroup_fd, NULL);
	if (!ASSERT_OK_PTR(link1, "attach_retval1"))
		goto done;

	link2 = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops_retval2, cgroup_fd, NULL);
	if (!ASSERT_OK_PTR(link2, "attach_retval2"))
		goto done;

	link3 = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops_retval3, cgroup_fd, NULL);
	if (!ASSERT_OK_PTR(link3, "attach_retval3"))
		goto done;

	do_listen_connect(AF_INET6);

	/* First prog inherits the kernel seed (no legacy sockops -> 0). */
	ASSERT_EQ(skel->bss->retval_saw1, 0, "retval_saw1");
	/* Each subsequent prog inherits the previous prog's return value. */
	ASSERT_EQ(skel->bss->retval_saw2, OPS_RETVAL1, "retval_saw2");
	ASSERT_EQ(skel->bss->retval_saw3, OPS_RETVAL2, "retval_saw3");

done:
	bpf_link__destroy(link3);
	bpf_link__destroy(link2);
	bpf_link__destroy(link1);
}

static void run_retval_subtest(void)
{
	struct bpf_tcp_ops *skel = NULL;
	struct netns_obj *ns = NULL;
	int cgroup_fd;

	cgroup_fd = test__join_cgroup(CGROUP_PATH);
	if (!ASSERT_GE(cgroup_fd, 0, "join_cgroup"))
		return;

	ns = netns_new(TEST_NETNS, true);
	if (!ASSERT_OK_PTR(ns, "netns_new"))
		goto done;

	skel = bpf_tcp_ops__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		goto done;

	test_retval(cgroup_fd, skel);

done:
	bpf_tcp_ops__destroy(skel);
	netns_free(ns);
	close(cgroup_fd);
}

/*
 * bpf_link__update_map() swaps the struct_ops map backing an attached link.
 * The link keeps its position, including BPF_F_PREORDER, across the update.
 * Attach ops1 (normal) and ops2 (preorder): order [2, 1]. Update the normal
 * link to ops3 -> [2, 3]; update the preorder link to ops1 -> [1, 3].
 */
static void test_update(int cgroup_fd, struct bpf_tcp_ops *skel)
{
	LIBBPF_OPTS(bpf_cgroup_opts, preorder_opts, .flags = BPF_F_PREORDER);
	struct bpf_link *link = NULL, *link_pre = NULL;

	link = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops1, cgroup_fd, NULL);
	if (!ASSERT_OK_PTR(link, "attach_ops1"))
		goto done;

	link_pre = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops2, cgroup_fd,
					       &preorder_opts);
	if (!ASSERT_OK_PTR(link_pre, "attach_ops2_preorder"))
		goto done;

	reset_order(skel);
	do_listen_connect(AF_INET6);
	ASSERT_EQ(skel->bss->listen_cnt, 2, "cnt_initial");
	ASSERT_EQ(skel->bss->listen_order[0], 2, "order0_initial");
	ASSERT_EQ(skel->bss->listen_order[1], 1, "order1_initial");

	/* Update the normal link's map (ops1 -> ops3); position is unchanged. */
	if (!ASSERT_OK(bpf_link__update_map(link, skel->maps.tcp_ops3), "update_normal"))
		goto done;

	reset_order(skel);
	do_listen_connect(AF_INET6);
	ASSERT_EQ(skel->bss->listen_order[0], 2, "order0_after_normal");
	ASSERT_EQ(skel->bss->listen_order[1], 3, "order1_after_normal");

	/* Update the preorder link's map (ops2 -> ops1); it stays first. */
	if (!ASSERT_OK(bpf_link__update_map(link_pre, skel->maps.tcp_ops1), "update_preorder"))
		goto done;

	reset_order(skel);
	do_listen_connect(AF_INET6);
	ASSERT_EQ(skel->bss->listen_order[0], 1, "order0_after_preorder");
	ASSERT_EQ(skel->bss->listen_order[1], 3, "order1_after_preorder");

done:
	bpf_link__destroy(link_pre);
	bpf_link__destroy(link);
}

static void run_update_subtest(void)
{
	struct bpf_tcp_ops *skel = NULL;
	struct netns_obj *ns = NULL;
	int cgroup_fd;

	cgroup_fd = test__join_cgroup(CGROUP_PATH);
	if (!ASSERT_GE(cgroup_fd, 0, "join_cgroup"))
		return;

	ns = netns_new(TEST_NETNS, true);
	if (!ASSERT_OK_PTR(ns, "netns_new"))
		goto done;

	skel = bpf_tcp_ops__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		goto done;

	test_update(cgroup_fd, skel);

done:
	bpf_tcp_ops__destroy(skel);
	netns_free(ns);
	close(cgroup_fd);
}

/*
 * Two-level hierarchy. Attach ops1 to the parent and ops2 to the child, then
 * trigger from a socket in the child. Descendant progs run before ancestor
 * progs, so the order is [2 (child), 1 (parent)].
 */
static void test_hierarchy(int parent_fd, int child_fd, struct bpf_tcp_ops *skel)
{
	struct bpf_link *plink = NULL, *clink = NULL;

	plink = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops1, parent_fd, NULL);
	if (!ASSERT_OK_PTR(plink, "attach_parent"))
		goto done;

	clink = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops2, child_fd, NULL);
	if (!ASSERT_OK_PTR(clink, "attach_child"))
		goto done;

	reset_order(skel);
	do_listen_connect(AF_INET6);

	ASSERT_EQ(skel->bss->listen_cnt, 2, "listen_cnt");
	ASSERT_EQ(skel->bss->listen_order[0], 2, "listen_order[0]");
	ASSERT_EQ(skel->bss->listen_order[1], 1, "listen_order[1]");

done:
	bpf_link__destroy(clink);
	bpf_link__destroy(plink);
}

static void run_hierarchy_subtest(void)
{
	struct bpf_tcp_ops *skel = NULL;
	struct netns_obj *ns = NULL;
	int parent_fd, child_fd = -1;

	parent_fd = test__join_cgroup(CGROUP_PATH);
	if (!ASSERT_GE(parent_fd, 0, "join_parent_cgroup"))
		return;

	child_fd = create_and_get_cgroup(CGROUP_PATH "/child");
	if (!ASSERT_GE(child_fd, 0, "create_child_cgroup"))
		goto done;

	if (!ASSERT_OK(join_cgroup(CGROUP_PATH "/child"), "join_child_cgroup"))
		goto done;

	ns = netns_new(TEST_NETNS, true);
	if (!ASSERT_OK_PTR(ns, "netns_new"))
		goto done;

	skel = bpf_tcp_ops__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		goto done;

	test_hierarchy(parent_fd, child_fd, skel);

done:
	bpf_tcp_ops__destroy(skel);
	netns_free(ns);
	if (child_fd >= 0) {
		close(child_fd);
		join_cgroup(CGROUP_PATH);
		remove_cgroup(CGROUP_PATH "/child");
	}
	close(parent_fd);
}

/*
 * Attach ops1 to the parent, then create and join the child cgroup. The child
 * is created after the attach, so it must inherit the parent's effective progs
 * via cgroup_bpf_inherit(). A socket in the child runs the parent's prog.
 */
static void test_inherit(int parent_fd, struct bpf_tcp_ops *skel)
{
	struct bpf_link *plink = NULL;
	int child_fd = -1;

	plink = bpf_map__attach_cgroup_opts(skel->maps.tcp_ops1, parent_fd, NULL);
	if (!ASSERT_OK_PTR(plink, "attach_parent"))
		goto done;

	child_fd = create_and_get_cgroup(CGROUP_PATH "/child");
	if (!ASSERT_GE(child_fd, 0, "create_child_cgroup"))
		goto done;

	if (!ASSERT_OK(join_cgroup(CGROUP_PATH "/child"), "join_child_cgroup"))
		goto done;

	reset_order(skel);
	do_listen_connect(AF_INET6);

	ASSERT_EQ(skel->bss->listen_cnt, 1, "listen_cnt");
	ASSERT_EQ(skel->bss->listen_order[0], 1, "listen_order[0]");

done:
	if (child_fd >= 0) {
		close(child_fd);
		join_cgroup(CGROUP_PATH);
		remove_cgroup(CGROUP_PATH "/child");
	}
	bpf_link__destroy(plink);
}

static void run_inherit_subtest(void)
{
	struct bpf_tcp_ops *skel = NULL;
	struct netns_obj *ns = NULL;
	int parent_fd;

	parent_fd = test__join_cgroup(CGROUP_PATH);
	if (!ASSERT_GE(parent_fd, 0, "join_parent_cgroup"))
		return;

	ns = netns_new(TEST_NETNS, true);
	if (!ASSERT_OK_PTR(ns, "netns_new"))
		goto done;

	skel = bpf_tcp_ops__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		goto done;

	test_inherit(parent_fd, skel);

done:
	bpf_tcp_ops__destroy(skel);
	netns_free(ns);
	close(parent_fd);
}

void test_bpf_tcp_ops(void)
{
	if (test__start_subtest("order"))
		run_order_subtest();
	if (test__start_subtest("before_after"))
		run_before_after_subtest();
	if (test__start_subtest("query"))
		run_query_subtest();
	if (test__start_subtest("retval"))
		run_retval_subtest();
	if (test__start_subtest("update"))
		run_update_subtest();
	if (test__start_subtest("hierarchy"))
		run_hierarchy_subtest();
	if (test__start_subtest("inherit"))
		run_inherit_subtest();
}
