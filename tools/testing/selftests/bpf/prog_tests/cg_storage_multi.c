// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright 2020 Google LLC.
 */

#include <test_progs.h>
#include <cgroup_helpers.h>
#include <network_helpers.h>

#include "progs/cg_storage_multi.h"

#include "cg_storage_multi_egress_only.skel.h"
#include "cg_storage_multi_isolated.skel.h"
#include "cg_storage_multi_shared.skel.h"

#define PARENT_CGROUP "/cgroup_storage"
#define CHILD_CGROUP "/cgroup_storage/child"

static int duration;

static bool assert_storage(struct bpf_map *map, const void *key,
			   struct cgroup_value *expected)
{
	struct cgroup_value value;
	int map_fd;

	map_fd = bpf_map__fd(map);

	if (CHECK(bpf_map_lookup_elem(map_fd, key, &value) < 0,
		  "map-lookup", "errno %d", errno))
		return true;
	if (CHECK(memcmp(&value, expected, sizeof(struct cgroup_value)),
		  "assert-storage", "storages differ"))
		return true;

	return false;
}

static bool assert_storage_noexist(struct bpf_map *map, const void *key)
{
	struct cgroup_value value;
	int map_fd;

	map_fd = bpf_map__fd(map);

	if (CHECK(bpf_map_lookup_elem(map_fd, key, &value) == 0,
		  "map-lookup", "succeeded, expected ENOENT"))
		return true;
	if (CHECK(errno != ENOENT,
		  "map-lookup", "errno %d, expected ENOENT", errno))
		return true;

	return false;
}

static bool connect_send(const char *cgroup_path)
{
	int server_fd = -1, client_fd = -1;
	char message[] = "message";
	bool res = true;

	if (join_cgroup(cgroup_path))
		goto out_clean;

	server_fd = start_server(AF_INET, SOCK_DGRAM, NULL, 0, 0);
	if (server_fd < 0)
		goto out_clean;

	client_fd = connect_to_fd(server_fd, 0);
	if (client_fd < 0)
		goto out_clean;

	if (send(client_fd, &message, sizeof(message), 0) < 0)
		goto out_clean;

	if (read(server_fd, &message, sizeof(message)) < 0)
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
	struct cgroup_value expected_cgroup_value;
	struct bpf_cgroup_storage_key key;
	struct bpf_link *parent_link = NULL, *child_link = NULL;
	bool err;

	key.attach_type = BPF_CGROUP_INET_EGRESS;

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
	key.cgroup_inode_id = get_cgroup_id(PARENT_CGROUP);
	expected_cgroup_value = (struct cgroup_value) { .egress_pkts = 1 };
	if (assert_storage(obj->maps.cgroup_storage,
			   &key, &expected_cgroup_value))
		goto close_bpf_object;
	key.cgroup_inode_id = get_cgroup_id(CHILD_CGROUP);
	if (assert_storage_noexist(obj->maps.cgroup_storage, &key))
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
	key.cgroup_inode_id = get_cgroup_id(PARENT_CGROUP);
	expected_cgroup_value = (struct cgroup_value) { .egress_pkts = 2 };
	if (assert_storage(obj->maps.cgroup_storage,
			   &key, &expected_cgroup_value))
		goto close_bpf_object;
	key.cgroup_inode_id = get_cgroup_id(CHILD_CGROUP);
	expected_cgroup_value = (struct cgroup_value) { .egress_pkts = 1 };
	if (assert_storage(obj->maps.cgroup_storage,
			   &key, &expected_cgroup_value))
		goto close_bpf_object;

close_bpf_object:
	if (!IS_ERR(parent_link))
		bpf_link__destroy(parent_link);
	if (!IS_ERR(child_link))
		bpf_link__destroy(child_link);

	cg_storage_multi_egress_only__destroy(obj);
}

static void test_isolated(int parent_cgroup_fd, int child_cgroup_fd)
{
	struct cg_storage_multi_isolated *obj;
	struct cgroup_value expected_cgroup_value;
	struct bpf_cgroup_storage_key key;
	struct bpf_link *parent_egress1_link = NULL, *parent_egress2_link = NULL;
	struct bpf_link *child_egress1_link = NULL, *child_egress2_link = NULL;
	struct bpf_link *parent_ingress_link = NULL, *child_ingress_link = NULL;
	bool err;

	obj = cg_storage_multi_isolated__open_and_load();
	if (CHECK(!obj, "skel-load", "errno %d", errno))
		return;

	/* Attach to parent cgroup, trigger packet from child.
	 * Assert that there is three runs, two with parent cgroup egress and
	 * one with parent cgroup ingress, stored in separate parent storages.
	 * Also assert that child cgroup's storages does not exist
	 */
	parent_egress1_link = bpf_program__attach_cgroup(obj->progs.egress1,
							 parent_cgroup_fd);
	if (CHECK(IS_ERR(parent_egress1_link), "parent-egress1-cg-attach",
		  "err %ld", PTR_ERR(parent_egress1_link)))
		goto close_bpf_object;
	parent_egress2_link = bpf_program__attach_cgroup(obj->progs.egress2,
							 parent_cgroup_fd);
	if (CHECK(IS_ERR(parent_egress2_link), "parent-egress2-cg-attach",
		  "err %ld", PTR_ERR(parent_egress2_link)))
		goto close_bpf_object;
	parent_ingress_link = bpf_program__attach_cgroup(obj->progs.ingress,
							 parent_cgroup_fd);
	if (CHECK(IS_ERR(parent_ingress_link), "parent-ingress-cg-attach",
		  "err %ld", PTR_ERR(parent_ingress_link)))
		goto close_bpf_object;
	err = connect_send(CHILD_CGROUP);
	if (CHECK(err, "first-connect-send", "errno %d", errno))
		goto close_bpf_object;
	if (CHECK(obj->bss->invocations != 3,
		  "first-invoke", "invocations=%d", obj->bss->invocations))
		goto close_bpf_object;
	key.cgroup_inode_id = get_cgroup_id(PARENT_CGROUP);
	key.attach_type = BPF_CGROUP_INET_EGRESS;
	expected_cgroup_value = (struct cgroup_value) { .egress_pkts = 2 };
	if (assert_storage(obj->maps.cgroup_storage,
			   &key, &expected_cgroup_value))
		goto close_bpf_object;
	key.attach_type = BPF_CGROUP_INET_INGRESS;
	expected_cgroup_value = (struct cgroup_value) { .ingress_pkts = 1 };
	if (assert_storage(obj->maps.cgroup_storage,
			   &key, &expected_cgroup_value))
		goto close_bpf_object;
	key.cgroup_inode_id = get_cgroup_id(CHILD_CGROUP);
	key.attach_type = BPF_CGROUP_INET_EGRESS;
	if (assert_storage_noexist(obj->maps.cgroup_storage, &key))
		goto close_bpf_object;
	key.attach_type = BPF_CGROUP_INET_INGRESS;
	if (assert_storage_noexist(obj->maps.cgroup_storage, &key))
		goto close_bpf_object;

	/* Attach to parent and child cgroup, trigger packet from child.
	 * Assert that there is six additional runs, parent cgroup egresses and
	 * ingress, child cgroup egresses and ingress.
	 * Assert that egree and ingress storages are separate.
	 */
	child_egress1_link = bpf_program__attach_cgroup(obj->progs.egress1,
							child_cgroup_fd);
	if (CHECK(IS_ERR(child_egress1_link), "child-egress1-cg-attach",
		  "err %ld", PTR_ERR(child_egress1_link)))
		goto close_bpf_object;
	child_egress2_link = bpf_program__attach_cgroup(obj->progs.egress2,
							child_cgroup_fd);
	if (CHECK(IS_ERR(child_egress2_link), "child-egress2-cg-attach",
		  "err %ld", PTR_ERR(child_egress2_link)))
		goto close_bpf_object;
	child_ingress_link = bpf_program__attach_cgroup(obj->progs.ingress,
							child_cgroup_fd);
	if (CHECK(IS_ERR(child_ingress_link), "child-ingress-cg-attach",
		  "err %ld", PTR_ERR(child_ingress_link)))
		goto close_bpf_object;
	err = connect_send(CHILD_CGROUP);
	if (CHECK(err, "second-connect-send", "errno %d", errno))
		goto close_bpf_object;
	if (CHECK(obj->bss->invocations != 9,
		  "second-invoke", "invocations=%d", obj->bss->invocations))
		goto close_bpf_object;
	key.cgroup_inode_id = get_cgroup_id(PARENT_CGROUP);
	key.attach_type = BPF_CGROUP_INET_EGRESS;
	expected_cgroup_value = (struct cgroup_value) { .egress_pkts = 4 };
	if (assert_storage(obj->maps.cgroup_storage,
			   &key, &expected_cgroup_value))
		goto close_bpf_object;
	key.attach_type = BPF_CGROUP_INET_INGRESS;
	expected_cgroup_value = (struct cgroup_value) { .ingress_pkts = 2 };
	if (assert_storage(obj->maps.cgroup_storage,
			   &key, &expected_cgroup_value))
		goto close_bpf_object;
	key.cgroup_inode_id = get_cgroup_id(CHILD_CGROUP);
	key.attach_type = BPF_CGROUP_INET_EGRESS;
	expected_cgroup_value = (struct cgroup_value) { .egress_pkts = 2 };
	if (assert_storage(obj->maps.cgroup_storage,
			   &key, &expected_cgroup_value))
		goto close_bpf_object;
	key.attach_type = BPF_CGROUP_INET_INGRESS;
	expected_cgroup_value = (struct cgroup_value) { .ingress_pkts = 1 };
	if (assert_storage(obj->maps.cgroup_storage,
			   &key, &expected_cgroup_value))
		goto close_bpf_object;

close_bpf_object:
	if (!IS_ERR(parent_egress1_link))
		bpf_link__destroy(parent_egress1_link);
	if (!IS_ERR(parent_egress2_link))
		bpf_link__destroy(parent_egress2_link);
	if (!IS_ERR(parent_ingress_link))
		bpf_link__destroy(parent_ingress_link);
	if (!IS_ERR(child_egress1_link))
		bpf_link__destroy(child_egress1_link);
	if (!IS_ERR(child_egress2_link))
		bpf_link__destroy(child_egress2_link);
	if (!IS_ERR(child_ingress_link))
		bpf_link__destroy(child_ingress_link);

	cg_storage_multi_isolated__destroy(obj);
}

static void test_shared(int parent_cgroup_fd, int child_cgroup_fd)
{
	struct cg_storage_multi_shared *obj;
	struct cgroup_value expected_cgroup_value;
	__u64 key;
	struct bpf_link *parent_egress1_link = NULL, *parent_egress2_link = NULL;
	struct bpf_link *child_egress1_link = NULL, *child_egress2_link = NULL;
	struct bpf_link *parent_ingress_link = NULL, *child_ingress_link = NULL;
	bool err;

	obj = cg_storage_multi_shared__open_and_load();
	if (CHECK(!obj, "skel-load", "errno %d", errno))
		return;

	/* Attach to parent cgroup, trigger packet from child.
	 * Assert that there is three runs, two with parent cgroup egress and
	 * one with parent cgroup ingress.
	 * Also assert that child cgroup's storage does not exist
	 */
	parent_egress1_link = bpf_program__attach_cgroup(obj->progs.egress1,
							 parent_cgroup_fd);
	if (CHECK(IS_ERR(parent_egress1_link), "parent-egress1-cg-attach",
		  "err %ld", PTR_ERR(parent_egress1_link)))
		goto close_bpf_object;
	parent_egress2_link = bpf_program__attach_cgroup(obj->progs.egress2,
							 parent_cgroup_fd);
	if (CHECK(IS_ERR(parent_egress2_link), "parent-egress2-cg-attach",
		  "err %ld", PTR_ERR(parent_egress2_link)))
		goto close_bpf_object;
	parent_ingress_link = bpf_program__attach_cgroup(obj->progs.ingress,
							 parent_cgroup_fd);
	if (CHECK(IS_ERR(parent_ingress_link), "parent-ingress-cg-attach",
		  "err %ld", PTR_ERR(parent_ingress_link)))
		goto close_bpf_object;
	err = connect_send(CHILD_CGROUP);
	if (CHECK(err, "first-connect-send", "errno %d", errno))
		goto close_bpf_object;
	if (CHECK(obj->bss->invocations != 3,
		  "first-invoke", "invocations=%d", obj->bss->invocations))
		goto close_bpf_object;
	key = get_cgroup_id(PARENT_CGROUP);
	expected_cgroup_value = (struct cgroup_value) {
		.egress_pkts = 2,
		.ingress_pkts = 1,
	};
	if (assert_storage(obj->maps.cgroup_storage,
			   &key, &expected_cgroup_value))
		goto close_bpf_object;
	key = get_cgroup_id(CHILD_CGROUP);
	if (assert_storage_noexist(obj->maps.cgroup_storage, &key))
		goto close_bpf_object;

	/* Attach to parent and child cgroup, trigger packet from child.
	 * Assert that there is six additional runs, parent cgroup egresses and
	 * ingress, child cgroup egresses and ingress.
	 */
	child_egress1_link = bpf_program__attach_cgroup(obj->progs.egress1,
							child_cgroup_fd);
	if (CHECK(IS_ERR(child_egress1_link), "child-egress1-cg-attach",
		  "err %ld", PTR_ERR(child_egress1_link)))
		goto close_bpf_object;
	child_egress2_link = bpf_program__attach_cgroup(obj->progs.egress2,
							child_cgroup_fd);
	if (CHECK(IS_ERR(child_egress2_link), "child-egress2-cg-attach",
		  "err %ld", PTR_ERR(child_egress2_link)))
		goto close_bpf_object;
	child_ingress_link = bpf_program__attach_cgroup(obj->progs.ingress,
							child_cgroup_fd);
	if (CHECK(IS_ERR(child_ingress_link), "child-ingress-cg-attach",
		  "err %ld", PTR_ERR(child_ingress_link)))
		goto close_bpf_object;
	err = connect_send(CHILD_CGROUP);
	if (CHECK(err, "second-connect-send", "errno %d", errno))
		goto close_bpf_object;
	if (CHECK(obj->bss->invocations != 9,
		  "second-invoke", "invocations=%d", obj->bss->invocations))
		goto close_bpf_object;
	key = get_cgroup_id(PARENT_CGROUP);
	expected_cgroup_value = (struct cgroup_value) {
		.egress_pkts = 4,
		.ingress_pkts = 2,
	};
	if (assert_storage(obj->maps.cgroup_storage,
			   &key, &expected_cgroup_value))
		goto close_bpf_object;
	key = get_cgroup_id(CHILD_CGROUP);
	expected_cgroup_value = (struct cgroup_value) {
		.egress_pkts = 2,
		.ingress_pkts = 1,
	};
	if (assert_storage(obj->maps.cgroup_storage,
			   &key, &expected_cgroup_value))
		goto close_bpf_object;

close_bpf_object:
	if (!IS_ERR(parent_egress1_link))
		bpf_link__destroy(parent_egress1_link);
	if (!IS_ERR(parent_egress2_link))
		bpf_link__destroy(parent_egress2_link);
	if (!IS_ERR(parent_ingress_link))
		bpf_link__destroy(parent_ingress_link);
	if (!IS_ERR(child_egress1_link))
		bpf_link__destroy(child_egress1_link);
	if (!IS_ERR(child_egress2_link))
		bpf_link__destroy(child_egress2_link);
	if (!IS_ERR(child_ingress_link))
		bpf_link__destroy(child_ingress_link);

	cg_storage_multi_shared__destroy(obj);
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

	if (test__start_subtest("isolated"))
		test_isolated(parent_cgroup_fd, child_cgroup_fd);

	if (test__start_subtest("shared"))
		test_shared(parent_cgroup_fd, child_cgroup_fd);

close_cgroup_fd:
	close(child_cgroup_fd);
	close(parent_cgroup_fd);
}
