// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "cgroup_helpers.h"
#include "network_helpers.h"

static int verify_ports(int family, int fd,
			__u16 expected_local, __u16 expected_peer)
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

	if (ntohs(port) != expected_local) {
		log_err("Unexpected local port %d, expected %d", ntohs(port),
			expected_local);
		return -1;
	}

	if (getpeername(fd, (struct sockaddr *)&addr, &len)) {
		log_err("Failed to get peer addr");
		return -1;
	}

	if (family == AF_INET)
		port = ((struct sockaddr_in *)&addr)->sin_port;
	else
		port = ((struct sockaddr_in6 *)&addr)->sin6_port;

	if (ntohs(port) != expected_peer) {
		log_err("Unexpected peer port %d, expected %d", ntohs(port),
			expected_peer);
		return -1;
	}

	return 0;
}

static int run_test(int cgroup_fd, int server_fd, int family, int type)
{
	bool v4 = family == AF_INET;
	__u16 expected_local_port = v4 ? 22222 : 22223;
	__u16 expected_peer_port = 60000;
	struct bpf_program *prog;
	struct bpf_object *obj;
	const char *obj_file = v4 ? "connect_force_port4.o" : "connect_force_port6.o";
	int fd, err;
	__u32 duration = 0;

	obj = bpf_object__open_file(obj_file, NULL);
	if (!ASSERT_OK_PTR(obj, "bpf_obj_open"))
		return -1;

	err = bpf_object__load(obj);
	if (!ASSERT_OK(err, "bpf_obj_load")) {
		err = -EIO;
		goto close_bpf_object;
	}

	prog = bpf_object__find_program_by_title(obj, v4 ?
						 "cgroup/connect4" :
						 "cgroup/connect6");
	if (CHECK(!prog, "find_prog", "connect prog not found\n")) {
		err = -EIO;
		goto close_bpf_object;
	}

	err = bpf_prog_attach(bpf_program__fd(prog), cgroup_fd, v4 ?
			      BPF_CGROUP_INET4_CONNECT :
			      BPF_CGROUP_INET6_CONNECT, 0);
	if (err) {
		log_err("Failed to attach BPF program");
		goto close_bpf_object;
	}

	prog = bpf_object__find_program_by_title(obj, v4 ?
						 "cgroup/getpeername4" :
						 "cgroup/getpeername6");
	if (CHECK(!prog, "find_prog", "getpeername prog not found\n")) {
		err = -EIO;
		goto close_bpf_object;
	}

	err = bpf_prog_attach(bpf_program__fd(prog), cgroup_fd, v4 ?
			      BPF_CGROUP_INET4_GETPEERNAME :
			      BPF_CGROUP_INET6_GETPEERNAME, 0);
	if (err) {
		log_err("Failed to attach BPF program");
		goto close_bpf_object;
	}

	prog = bpf_object__find_program_by_title(obj, v4 ?
						 "cgroup/getsockname4" :
						 "cgroup/getsockname6");
	if (CHECK(!prog, "find_prog", "getsockname prog not found\n")) {
		err = -EIO;
		goto close_bpf_object;
	}

	err = bpf_prog_attach(bpf_program__fd(prog), cgroup_fd, v4 ?
			      BPF_CGROUP_INET4_GETSOCKNAME :
			      BPF_CGROUP_INET6_GETSOCKNAME, 0);
	if (err) {
		log_err("Failed to attach BPF program");
		goto close_bpf_object;
	}

	fd = connect_to_fd(server_fd, 0);
	if (fd < 0) {
		err = -1;
		goto close_bpf_object;
	}

	err = verify_ports(family, fd, expected_local_port,
			   expected_peer_port);
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

	server_fd = start_server(AF_INET, SOCK_STREAM, NULL, 60123, 0);
	if (CHECK_FAIL(server_fd < 0))
		goto close_cgroup_fd;
	CHECK_FAIL(run_test(cgroup_fd, server_fd, AF_INET, SOCK_STREAM));
	close(server_fd);

	server_fd = start_server(AF_INET6, SOCK_STREAM, NULL, 60124, 0);
	if (CHECK_FAIL(server_fd < 0))
		goto close_cgroup_fd;
	CHECK_FAIL(run_test(cgroup_fd, server_fd, AF_INET6, SOCK_STREAM));
	close(server_fd);

	server_fd = start_server(AF_INET, SOCK_DGRAM, NULL, 60123, 0);
	if (CHECK_FAIL(server_fd < 0))
		goto close_cgroup_fd;
	CHECK_FAIL(run_test(cgroup_fd, server_fd, AF_INET, SOCK_DGRAM));
	close(server_fd);

	server_fd = start_server(AF_INET6, SOCK_DGRAM, NULL, 60124, 0);
	if (CHECK_FAIL(server_fd < 0))
		goto close_cgroup_fd;
	CHECK_FAIL(run_test(cgroup_fd, server_fd, AF_INET6, SOCK_DGRAM));
	close(server_fd);

close_cgroup_fd:
	close(cgroup_fd);
}
