// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "cgroup_helpers.h"
#include "network_helpers.h"
#include "cgroup_storage.skel.h"

#define TEST_CGROUP "/test-bpf-cgroup-storage-buf/"
#define TEST_NS "cgroup_storage_ns"
#define PING_CMD "ping localhost -c 1 -W 1 -q"

static int setup_network(struct nstoken **token)
{
	SYS(fail, "ip netns add %s", TEST_NS);
	*token = open_netns(TEST_NS);
	if (!ASSERT_OK_PTR(*token, "open netns"))
		goto cleanup_ns;
	SYS(cleanup_ns, "ip link set lo up");

	return 0;

cleanup_ns:
	SYS_NOFAIL("ip netns del %s", TEST_NS);
fail:
	return -1;
}

static void cleanup_network(struct nstoken *ns)
{
	close_netns(ns);
	SYS_NOFAIL("ip netns del %s", TEST_NS);
}

void test_cgroup_storage(void)
{
	struct bpf_cgroup_storage_key key;
	struct cgroup_storage *skel;
	struct nstoken *ns = NULL;
	unsigned long long value;
	int cgroup_fd;
	int err;

	cgroup_fd = cgroup_setup_and_join(TEST_CGROUP);
	if (!ASSERT_OK_FD(cgroup_fd, "create cgroup"))
		return;

	if (!ASSERT_OK(setup_network(&ns), "setup network"))
		goto cleanup_cgroup;

	skel = cgroup_storage__open_and_load();
	if (!ASSERT_OK_PTR(skel, "load program"))
		goto cleanup_network;

	skel->links.bpf_prog =
		bpf_program__attach_cgroup(skel->progs.bpf_prog, cgroup_fd);
	if (!ASSERT_OK_PTR(skel->links.bpf_prog, "attach program"))
		goto cleanup_progs;

	/* Check that one out of every two packets is dropped */
	err = SYS_NOFAIL(PING_CMD);
	ASSERT_OK(err, "first ping");
	err = SYS_NOFAIL(PING_CMD);
	ASSERT_NEQ(err, 0, "second ping");
	err = SYS_NOFAIL(PING_CMD);
	ASSERT_OK(err, "third ping");

	err = bpf_map__get_next_key(skel->maps.cgroup_storage, NULL, &key,
				    sizeof(key));
	if (!ASSERT_OK(err, "get first key"))
		goto cleanup_progs;
	err = bpf_map__lookup_elem(skel->maps.cgroup_storage, &key, sizeof(key),
				   &value, sizeof(value), 0);
	if (!ASSERT_OK(err, "first packet count read"))
		goto cleanup_progs;

	/* Add one to the packet counter, check again packet filtering */
	value++;
	err = bpf_map__update_elem(skel->maps.cgroup_storage, &key, sizeof(key),
				   &value, sizeof(value), 0);
	if (!ASSERT_OK(err, "increment packet counter"))
		goto cleanup_progs;
	err = SYS_NOFAIL(PING_CMD);
	ASSERT_OK(err, "fourth ping");
	err = SYS_NOFAIL(PING_CMD);
	ASSERT_NEQ(err, 0, "fifth ping");
	err = SYS_NOFAIL(PING_CMD);
	ASSERT_OK(err, "sixth ping");

cleanup_progs:
	cgroup_storage__destroy(skel);
cleanup_network:
	cleanup_network(ns);
cleanup_cgroup:
	close(cgroup_fd);
	cleanup_cgroup_environment();
}
