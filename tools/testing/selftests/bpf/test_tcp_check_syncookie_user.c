// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook
// Copyright (c) 2019 Cloudflare

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "bpf_rlimit.h"
#include "cgroup_helpers.h"

static int start_server(const struct sockaddr *addr, socklen_t len)
{
	int fd;

	fd = socket(addr->sa_family, SOCK_STREAM, 0);
	if (fd == -1) {
		log_err("Failed to create server socket");
		goto out;
	}

	if (bind(fd, addr, len) == -1) {
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
	int fd = -1;

	if (getsockname(server_fd, (struct sockaddr *)&addr, &len)) {
		log_err("Failed to get server addr");
		goto out;
	}

	fd = socket(addr.ss_family, SOCK_STREAM, 0);
	if (fd == -1) {
		log_err("Failed to create client socket");
		goto out;
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

static int get_map_fd_by_prog_id(int prog_id)
{
	struct bpf_prog_info info = {};
	__u32 info_len = sizeof(info);
	__u32 map_ids[1];
	int prog_fd = -1;
	int map_fd = -1;

	prog_fd = bpf_prog_get_fd_by_id(prog_id);
	if (prog_fd < 0) {
		log_err("Failed to get fd by prog id %d", prog_id);
		goto err;
	}

	info.nr_map_ids = 1;
	info.map_ids = (__u64)(unsigned long)map_ids;

	if (bpf_obj_get_info_by_fd(prog_fd, &info, &info_len)) {
		log_err("Failed to get info by prog fd %d", prog_fd);
		goto err;
	}

	if (!info.nr_map_ids) {
		log_err("No maps found for prog fd %d", prog_fd);
		goto err;
	}

	map_fd = bpf_map_get_fd_by_id(map_ids[0]);
	if (map_fd < 0)
		log_err("Failed to get fd by map id %d", map_ids[0]);
err:
	if (prog_fd >= 0)
		close(prog_fd);
	return map_fd;
}

static int run_test(int server_fd, int results_fd)
{
	int client = -1, srv_client = -1;
	int ret = 0;
	__u32 key = 0;
	__u64 value = 0;

	if (bpf_map_update_elem(results_fd, &key, &value, 0) < 0) {
		log_err("Can't clear results");
		goto err;
	}

	client = connect_to_server(server_fd);
	if (client == -1)
		goto err;

	srv_client = accept(server_fd, NULL, 0);
	if (srv_client == -1) {
		log_err("Can't accept connection");
		goto err;
	}

	if (bpf_map_lookup_elem(results_fd, &key, &value) < 0) {
		log_err("Can't lookup result");
		goto err;
	}

	if (value != 1) {
		log_err("Didn't match syncookie: %llu", value);
		goto err;
	}

	goto out;

err:
	ret = 1;
out:
	close(client);
	close(srv_client);
	return ret;
}

int main(int argc, char **argv)
{
	struct sockaddr_in addr4;
	struct sockaddr_in6 addr6;
	int server = -1;
	int server_v6 = -1;
	int results = -1;
	int err = 0;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s prog_id\n", argv[0]);
		exit(1);
	}

	results = get_map_fd_by_prog_id(atoi(argv[1]));
	if (results < 0) {
		log_err("Can't get map");
		goto err;
	}

	memset(&addr4, 0, sizeof(addr4));
	addr4.sin_family = AF_INET;
	addr4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr4.sin_port = 0;

	memset(&addr6, 0, sizeof(addr6));
	addr6.sin6_family = AF_INET6;
	addr6.sin6_addr = in6addr_loopback;
	addr6.sin6_port = 0;

	server = start_server((const struct sockaddr *)&addr4, sizeof(addr4));
	if (server == -1)
		goto err;

	server_v6 = start_server((const struct sockaddr *)&addr6,
				 sizeof(addr6));
	if (server_v6 == -1)
		goto err;

	if (run_test(server, results))
		goto err;

	if (run_test(server_v6, results))
		goto err;

	printf("ok\n");
	goto out;
err:
	err = 1;
out:
	close(server);
	close(server_v6);
	close(results);
	return err;
}
