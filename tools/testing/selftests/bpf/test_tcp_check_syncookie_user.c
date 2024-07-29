// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook
// Copyright (c) 2019 Cloudflare

#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "cgroup_helpers.h"
#include "network_helpers.h"

static int get_map_fd_by_prog_id(int prog_id, bool *xdp)
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

	if (bpf_prog_get_info_by_fd(prog_fd, &info, &info_len)) {
		log_err("Failed to get info by prog fd %d", prog_fd);
		goto err;
	}

	if (!info.nr_map_ids) {
		log_err("No maps found for prog fd %d", prog_fd);
		goto err;
	}

	*xdp = info.type == BPF_PROG_TYPE_XDP;

	map_fd = bpf_map_get_fd_by_id(map_ids[0]);
	if (map_fd < 0)
		log_err("Failed to get fd by map id %d", map_ids[0]);
err:
	if (prog_fd >= 0)
		close(prog_fd);
	return map_fd;
}

static int run_test(int server_fd, int results_fd, bool xdp)
{
	int client = -1, srv_client = -1;
	int ret = 0;
	__u32 key = 0;
	__u32 key_gen = 1;
	__u32 key_mss = 2;
	__u32 value = 0;
	__u32 value_gen = 0;
	__u32 value_mss = 0;

	if (bpf_map_update_elem(results_fd, &key, &value, 0) < 0) {
		log_err("Can't clear results");
		goto err;
	}

	if (bpf_map_update_elem(results_fd, &key_gen, &value_gen, 0) < 0) {
		log_err("Can't clear results");
		goto err;
	}

	if (bpf_map_update_elem(results_fd, &key_mss, &value_mss, 0) < 0) {
		log_err("Can't clear results");
		goto err;
	}

	client = connect_to_fd(server_fd, 0);
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

	if (value == 0) {
		log_err("Didn't match syncookie: %u", value);
		goto err;
	}

	if (bpf_map_lookup_elem(results_fd, &key_gen, &value_gen) < 0) {
		log_err("Can't lookup result");
		goto err;
	}

	if (xdp && value_gen == 0) {
		// SYN packets do not get passed through generic XDP, skip the
		// rest of the test.
		printf("Skipping XDP cookie check\n");
		goto out;
	}

	if (bpf_map_lookup_elem(results_fd, &key_mss, &value_mss) < 0) {
		log_err("Can't lookup result");
		goto err;
	}

	if (value != value_gen) {
		log_err("BPF generated cookie does not match kernel one");
		goto err;
	}

	if (value_mss < 536 || value_mss > USHRT_MAX) {
		log_err("Unexpected MSS retrieved");
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

static int v6only_true(int fd, void *opts)
{
	int mode = true;

	return setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &mode, sizeof(mode));
}

static int v6only_false(int fd, void *opts)
{
	int mode = false;

	return setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &mode, sizeof(mode));
}

int main(int argc, char **argv)
{
	struct network_helper_opts opts = { 0 };
	int server = -1;
	int server_v6 = -1;
	int server_dual = -1;
	int results = -1;
	int err = 0;
	bool xdp;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s prog_id\n", argv[0]);
		exit(1);
	}

	/* Use libbpf 1.0 API mode */
	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

	results = get_map_fd_by_prog_id(atoi(argv[1]), &xdp);
	if (results < 0) {
		log_err("Can't get map");
		goto err;
	}

	server = start_server_str(AF_INET, SOCK_STREAM, "127.0.0.1", 0, NULL);
	if (server == -1)
		goto err;

	opts.post_socket_cb = v6only_true;
	server_v6 = start_server_str(AF_INET6, SOCK_STREAM, "::1", 0, &opts);
	if (server_v6 == -1)
		goto err;

	opts.post_socket_cb = v6only_false;
	server_dual = start_server_str(AF_INET6, SOCK_STREAM, "::0", 0, &opts);
	if (server_dual == -1)
		goto err;

	if (run_test(server, results, xdp))
		goto err;

	if (run_test(server_v6, results, xdp))
		goto err;

	if (run_test(server_dual, results, xdp))
		goto err;

	printf("ok\n");
	goto out;
err:
	err = 1;
out:
	close(server);
	close(server_v6);
	close(server_dual);
	close(results);
	return err;
}
