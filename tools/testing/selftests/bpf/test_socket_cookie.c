// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "bpf_rlimit.h"
#include "cgroup_helpers.h"

#define CG_PATH			"/foo"
#define SOCKET_COOKIE_PROG	"./socket_cookie_prog.o"

struct socket_cookie {
	__u64 cookie_key;
	__u32 cookie_value;
};

static int start_server(void)
{
	struct sockaddr_in6 addr;
	int fd;

	fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd == -1) {
		log_err("Failed to create server socket");
		goto out;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin6_family = AF_INET6;
	addr.sin6_addr = in6addr_loopback;
	addr.sin6_port = 0;

	if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) == -1) {
		log_err("Failed to bind server socket");
		goto close_out;
	}

	if (listen(fd, 128) == -1) {
		log_err("Failed to listen on server socket");
		goto close_out;
	}

	goto out;

close_out:
	close(fd);
	fd = -1;
out:
	return fd;
}

static int connect_to_server(int server_fd)
{
	struct sockaddr_storage addr;
	socklen_t len = sizeof(addr);
	int fd;

	fd = socket(AF_INET6, SOCK_STREAM, 0);
	if (fd == -1) {
		log_err("Failed to create client socket");
		goto out;
	}

	if (getsockname(server_fd, (struct sockaddr *)&addr, &len)) {
		log_err("Failed to get server addr");
		goto close_out;
	}

	if (connect(fd, (const struct sockaddr *)&addr, len) == -1) {
		log_err("Fail to connect to server");
		goto close_out;
	}

	goto out;

close_out:
	close(fd);
	fd = -1;
out:
	return fd;
}

static int validate_map(struct bpf_map *map, int client_fd)
{
	__u32 cookie_expected_value;
	struct sockaddr_in6 addr;
	socklen_t len = sizeof(addr);
	struct socket_cookie val;
	int err = 0;
	int map_fd;

	if (!map) {
		log_err("Map not found in BPF object");
		goto err;
	}

	map_fd = bpf_map__fd(map);

	err = bpf_map_lookup_elem(map_fd, &client_fd, &val);

	err = getsockname(client_fd, (struct sockaddr *)&addr, &len);
	if (err) {
		log_err("Can't get client local addr");
		goto out;
	}

	cookie_expected_value = (ntohs(addr.sin6_port) << 8) | 0xFF;
	if (val.cookie_value != cookie_expected_value) {
		log_err("Unexpected value in map: %x != %x", val.cookie_value,
			cookie_expected_value);
		goto err;
	}

	goto out;
err:
	err = -1;
out:
	return err;
}

static int run_test(int cgfd)
{
	enum bpf_attach_type attach_type;
	struct bpf_prog_load_attr attr;
	struct bpf_program *prog;
	struct bpf_object *pobj;
	const char *prog_name;
	int server_fd = -1;
	int client_fd = -1;
	int prog_fd = -1;
	int err = 0;

	memset(&attr, 0, sizeof(attr));
	attr.file = SOCKET_COOKIE_PROG;
	attr.prog_type = BPF_PROG_TYPE_UNSPEC;
	attr.prog_flags = BPF_F_TEST_RND_HI32;

	err = bpf_prog_load_xattr(&attr, &pobj, &prog_fd);
	if (err) {
		log_err("Failed to load %s", attr.file);
		goto out;
	}

	bpf_object__for_each_program(prog, pobj) {
		prog_name = bpf_program__section_name(prog);

		if (libbpf_attach_type_by_name(prog_name, &attach_type))
			goto err;

		err = bpf_prog_attach(bpf_program__fd(prog), cgfd, attach_type,
				      BPF_F_ALLOW_OVERRIDE);
		if (err) {
			log_err("Failed to attach prog %s", prog_name);
			goto out;
		}
	}

	server_fd = start_server();
	if (server_fd == -1)
		goto err;

	client_fd = connect_to_server(server_fd);
	if (client_fd == -1)
		goto err;

	if (validate_map(bpf_map__next(NULL, pobj), client_fd))
		goto err;

	goto out;
err:
	err = -1;
out:
	close(client_fd);
	close(server_fd);
	bpf_object__close(pobj);
	printf("%s\n", err ? "FAILED" : "PASSED");
	return err;
}

int main(int argc, char **argv)
{
	int cgfd = -1;
	int err = 0;

	cgfd = cgroup_setup_and_join(CG_PATH);
	if (cgfd < 0)
		goto err;

	if (run_test(cgfd))
		goto err;

	goto out;
err:
	err = -1;
out:
	close(cgfd);
	cleanup_cgroup_environment();
	return err;
}
