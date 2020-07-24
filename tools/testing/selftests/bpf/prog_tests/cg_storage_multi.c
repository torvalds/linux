// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright 2020 Google LLC.
 */

#include <test_progs.h>
#include <cgroup_helpers.h>
#include <network_helpers.h>

#include "cg_storage_multi_egress_only.skel.h"

#define PARENT_CGROUP "/cgroup_storage"
#define CHILD_CGROUP "/cgroup_storage/child"

static int duration;

static bool assert_storage(struct bpf_map *map, const char *cgroup_path,
			   __u32 expected)
{
	struct bpf_cgroup_storage_key key = {0};
	__u32 value;
	int map_fd;

	map_fd = bpf_map__fd(map);

	key.cgroup_inode_id = get_cgroup_id(cgroup_path);
	key.attach_type = BPF_CGROUP_INET_EGRESS;
	if (CHECK(bpf_map_lookup_elem(map_fd, &key, &value) < 0,
		  "map-lookup", "errno %d", errno))
		return true;
	if (CHECK(value != expected,
		  "assert-storage", "got %u expected %u", value, expected))
		return true;

	return false;
}

static bool assert_storage_noexist(struct bpf_map *map, const char *cgroup_path)
{
	struct bpf_cgroup_storage_key key = {0};
	__u32 value;
	int map_fd;

	map_fd = bpf_map__fd(map);

	key.cgroup_inode_id = get_cgroup_id(cgroup_path);
	key.attach_type = BPF_CGROUP_INET_EGRESS;
	if (CHECK(bpf_map_lookup_elem(map_fd, &key, &value) == 0,
		  "map-lookup", "succeeded, expected ENOENT"))
		return true;
	if (CHECK(errno != ENOENT,
		  "map-lookup", "errno %d, expected ENOENT", errno))
		return true;

	return false;
}

static bool connect_send(const char *cgroup_path)
{
	bool res = true;
	int server_fd = -1, client_fd = -1;

	if (join_cgroup(cgroup_path))
		goto out_clean;

	server_fd = start_server(AF_INET, SOCK_DGRAM, NULL, 0, 0);
	if (server_fd < 0)
		goto out_clean;

	client_fd = connect_to_fd(server_fd, 0);
	if (client_fd < 0)
		goto out_clean;

	if (send(client_fd, "message", strlen("message"), 0) < 0)
		goto out_clean;

	res = false;

out_clean:
	close(client_fd);
	close(server_fd);
	return res;
}

static void test_egress_only(int parent_cgroup_fd, int child_cgroup_fd)
{
	struct cg_storage_multi_egress_only *obj;
	struct bpf_link *parent_link = NULL, *child_link = NULL;
	bool err;

	obj = cg_storage_multi_egress_only__open_and_load();
	if (CHECK(!obj, "skel-load", "errno %d", errno))
		return;

	/* Attach to parent cgroup, trigger packet from child.
	 * Assert that there is only one run and in that run the storage is
	 * parent cgroup's storage.
	 * Also assert that child cgroup's storage does not exist
	 */
	parent_link = bpf_program__attach_cgroup(obj->progs.egress,
						 parent_cgroup_fd);
	if (CHECK(IS_ERR(parent_link), "parent-cg-attach",
		  "err %ld", PTR_ERR(parent_link)))
		goto close_bpf_object;
	err = connect_send(CHILD_CGROUP);
	if (CHECK(err, "first-connect-send", "errno %d", errno))
		goto close_bpf_object;
	if (CHECK(obj->bss->invocations != 1,
		  "first-invoke", "invocations=%d", obj->bss->invocations))
		goto close_bpf_object;
	if (assert_storage(obj->maps.cgroup_storage, PARENT_CGROUP, 1))
		goto close_bpf_object;
	if (assert_storage_noexist(obj->maps.cgroup_storage, CHILD_CGROUP))
		goto close_bpf_object;

	/* Attach to parent and child cgroup, trigger packet from child.
	 * Assert that there are two additional runs, one that run with parent
	 * cgroup's storage and one with child cgroup's storage.
	 */
	child_link = bpf_program__attach_cgroup(obj->progs.egress,
						child_cgroup_fd);
	if (CHECK(IS_ERR(child_link), "child-cg-attach",
		  "err %ld", PTR_ERR(child_link)))
		goto close_bpf_object;
	err = connect_send(CHILD_CGROUP);
	if (CHECK(err, "second-connect-send", "errno %d", errno))
		goto close_bpf_object;
	if (CHECK(obj->bss->invocations != 3,
		  "second-invoke", "invocations=%d", obj->bss->invocations))
		goto close_bpf_object;
	if (assert_storage(obj->maps.cgroup_storage, PARENT_CGROUP, 2))
		goto close_bpf_object;
	if (assert_storage(obj->maps.cgroup_storage, CHILD_CGROUP, 1))
		goto close_bpf_object;

close_bpf_object:
	bpf_link__destroy(parent_link);
	bpf_link__destroy(child_link);

	cg_storage_multi_egress_only__destroy(obj);
}

void test_cg_storage_multi(void)
{
	int parent_cgroup_fd = -1, child_cgroup_fd = -1;

	parent_cgroup_fd = test__join_cgroup(PARENT_CGROUP);
	if (CHECK(parent_cgroup_fd < 0, "cg-create-parent", "errno %d", errno))
		goto close_cgroup_fd;
	child_cgroup_fd = create_and_get_cgroup(CHILD_CGROUP);
	if (CHECK(child_cgroup_fd < 0, "cg-create-child", "errno %d", errno))
		goto close_cgroup_fd;

	if (test__start_subtest("egress_only"))
		test_egress_only(parent_cgroup_fd, child_cgroup_fd);

close_cgroup_fd:
	close(child_cgroup_fd);
	close(parent_cgroup_fd);
}
