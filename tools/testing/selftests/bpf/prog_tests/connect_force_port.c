// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "cgroup_helpers.h"
#include "network_helpers.h"

static int verify_port(int family, int fd, int expected)
{
	struct sockaddr_storage addr;
	socklen_t len = sizeof(addr);
	__u16 port;

	if (getsockname(fd, (struct sockaddr *)&addr, &len)) {
		log_err("Failed to get server addr");
		return -1;
	}

	if (family == AF_INET)
		port = ((struct sockaddr_in *)&addr)->sin_port;
	else
		port = ((struct sockaddr_in6 *)&addr)->sin6_port;

	if (ntohs(port) != expected) {
		log_err("Unexpected port %d, expected %d", ntohs(port),
			expected);
		return -1;
	}

	return 0;
}

static int run_test(int cgroup_fd, int server_fd, int family, int type)
{
	struct bpf_prog_load_attr attr = {
		.prog_type = BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
	};
	struct bpf_object *obj;
	int expected_port;
	int prog_fd;
	int err;
	int fd;

	if (family == AF_INET) {
		attr.file = "./connect_force_port4.o";
		attr.expected_attach_type = BPF_CGROUP_INET4_CONNECT;
		expected_port = 22222;
	} else {
		attr.file = "./connect_force_port6.o";
		attr.expected_attach_type = BPF_CGROUP_INET6_CONNECT;
		expected_port = 22223;
	}

	err = bpf_prog_load_xattr(&attr, &obj, &prog_fd);
	if (err) {
		log_err("Failed to load BPF object");
		return -1;
	}

	err = bpf_prog_attach(prog_fd, cgroup_fd, attr.expected_attach_type,
			      0);
	if (err) {
		log_err("Failed to attach BPF program");
		goto close_bpf_object;
	}

	fd = connect_to_fd(family, type, server_fd);
	if (fd < 0) {
		err = -1;
		goto close_bpf_object;
	}

	err = verify_port(family, fd, expected_port);

	close(fd);

close_bpf_object:
	bpf_object__close(obj);
	return err;
}

void test_connect_force_port(void)
{
	int server_fd, cgroup_fd;

	cgroup_fd = test__join_cgroup("/connect_force_port");
	if (CHECK_FAIL(cgroup_fd < 0))
		return;

	server_fd = start_server(AF_INET, SOCK_STREAM);
	if (CHECK_FAIL(server_fd < 0))
		goto close_cgroup_fd;
	CHECK_FAIL(run_test(cgroup_fd, server_fd, AF_INET, SOCK_STREAM));
	close(server_fd);

	server_fd = start_server(AF_INET6, SOCK_STREAM);
	if (CHECK_FAIL(server_fd < 0))
		goto close_cgroup_fd;
	CHECK_FAIL(run_test(cgroup_fd, server_fd, AF_INET6, SOCK_STREAM));
	close(server_fd);

	server_fd = start_server(AF_INET, SOCK_DGRAM);
	if (CHECK_FAIL(server_fd < 0))
		goto close_cgroup_fd;
	CHECK_FAIL(run_test(cgroup_fd, server_fd, AF_INET, SOCK_DGRAM));
	close(server_fd);

	server_fd = start_server(AF_INET6, SOCK_DGRAM);
	if (CHECK_FAIL(server_fd < 0))
		goto close_cgroup_fd;
	CHECK_FAIL(run_test(cgroup_fd, server_fd, AF_INET6, SOCK_DGRAM));
	close(server_fd);

close_cgroup_fd:
	close(cgroup_fd);
}
