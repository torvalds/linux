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

#include "bpf_rlimit.h"
#include "cgroup_helpers.h"

static int start_server(const struct sockaddr *addr, socklen_t len, bool dual)
{
	int mode = !dual;
	int fd;

	fd = socket(addr->sa_family, SOCK_STREAM, 0);
	if (fd == -1) {
		log_err("Failed to create server socket");
		goto out;
	}

	if (addr->sa_family == AF_INET6) {
		if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&mode,
			       sizeof(mode)) == -1) {
			log_err("Failed to set the dual-stack mode");
			goto close_out;
		}
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

static int connect_to_server(const struct sockaddr *addr, socklen_t len)
{
	int fd = -1;

	fd = socket(addr->sa_family, SOCK_STREAM, 0);
	if (fd == -1) {
		log_err("Failed to create client socket");
		goto out;
	}

	if (connect(fd, (const struct sockaddr *)addr, len) == -1) {
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

	if (bpf_obj_get_info_by_fd(prog_fd, &info, &info_len)) {
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

static int run_test(int server_fd, int results_fd, bool xdp,
		    const struct sockaddr *addr, socklen_t len)
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

	client = connect_to_server(addr, len);
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

static bool get_port(int server_fd, in_port_t *port)
{
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);

	if (getsockname(server_fd, (struct sockaddr *)&addr, &len)) {
		log_err("Failed to get server addr");
		return false;
	}

	/* sin_port and sin6_port are located at the same offset. */
	*port = addr.sin_port;
	return true;
}

int main(int argc, char **argv)
{
	struct sockaddr_in addr4;
	struct sockaddr_in6 addr6;
	struct sockaddr_in addr4dual;
	struct sockaddr_in6 addr6dual;
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

	results = get_map_fd_by_prog_id(atoi(argv[1]), &xdp);
	if (results < 0) {
		log_err("Can't get map");
		goto err;
	}

	memset(&addr4, 0, sizeof(addr4));
	addr4.sin_family = AF_INET;
	addr4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr4.sin_port = 0;
	memcpy(&addr4dual, &addr4, sizeof(addr4dual));

	memset(&addr6, 0, sizeof(addr6));
	addr6.sin6_family = AF_INET6;
	addr6.sin6_addr = in6addr_loopback;
	addr6.sin6_port = 0;

	memset(&addr6dual, 0, sizeof(addr6dual));
	addr6dual.sin6_family = AF_INET6;
	addr6dual.sin6_addr = in6addr_any;
	addr6dual.sin6_port = 0;

	server = start_server((const struct sockaddr *)&addr4, sizeof(addr4),
			      false);
	if (server == -1 || !get_port(server, &addr4.sin_port))
		goto err;

	server_v6 = start_server((const struct sockaddr *)&addr6,
				 sizeof(addr6), false);
	if (server_v6 == -1 || !get_port(server_v6, &addr6.sin6_port))
		goto err;

	server_dual = start_server((const struct sockaddr *)&addr6dual,
				   sizeof(addr6dual), true);
	if (server_dual == -1 || !get_port(server_dual, &addr4dual.sin_port))
		goto err;

	if (run_test(server, results, xdp,
		     (const struct sockaddr *)&addr4, sizeof(addr4)))
		goto err;

	if (run_test(server_v6, results, xdp,
		     (const struct sockaddr *)&addr6, sizeof(addr6)))
		goto err;

	if (run_test(server_dual, results, xdp,
		     (const struct sockaddr *)&addr4dual, sizeof(addr4dual)))
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
