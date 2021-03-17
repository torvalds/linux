// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright 2020 Google LLC.
 */

#include <test_progs.h>
#include <network_helpers.h>

void test_load_bytes_relative(void)
{
	int server_fd, cgroup_fd, prog_fd, map_fd, client_fd;
	int err;
	struct bpf_object *obj;
	struct bpf_program *prog;
	struct bpf_map *test_result;
	__u32 duration = 0;

	__u32 map_key = 0;
	__u32 map_value = 0;

	cgroup_fd = test__join_cgroup("/load_bytes_relative");
	if (CHECK_FAIL(cgroup_fd < 0))
		return;

	server_fd = start_server(AF_INET, SOCK_STREAM, NULL, 0, 0);
	if (CHECK_FAIL(server_fd < 0))
		goto close_cgroup_fd;

	err = bpf_prog_load("./load_bytes_relative.o", BPF_PROG_TYPE_CGROUP_SKB,
			    &obj, &prog_fd);
	if (CHECK_FAIL(err))
		goto close_server_fd;

	test_result = bpf_object__find_map_by_name(obj, "test_result");
	if (CHECK_FAIL(!test_result))
		goto close_bpf_object;

	map_fd = bpf_map__fd(test_result);
	if (map_fd < 0)
		goto close_bpf_object;

	prog = bpf_object__find_program_by_name(obj, "load_bytes_relative");
	if (CHECK_FAIL(!prog))
		goto close_bpf_object;

	err = bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_INET_EGRESS,
			      BPF_F_ALLOW_MULTI);
	if (CHECK_FAIL(err))
		goto close_bpf_object;

	client_fd = connect_to_fd(server_fd, 0);
	if (CHECK_FAIL(client_fd < 0))
		goto close_bpf_object;
	close(client_fd);

	err = bpf_map_lookup_elem(map_fd, &map_key, &map_value);
	if (CHECK_FAIL(err))
		goto close_bpf_object;

	CHECK(map_value != 1, "bpf", "bpf program returned failure");

close_bpf_object:
	bpf_object__close(obj);

close_server_fd:
	close(server_fd);

close_cgroup_fd:
	close(cgroup_fd);
}
