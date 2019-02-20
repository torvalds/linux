// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018 Facebook

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>


#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "bpf_rlimit.h"
#include "cgroup_helpers.h"

#define CGROUP_PATH		"/skb_cgroup_test"
#define NUM_CGROUP_LEVELS	4

/* RFC 4291, Section 2.7.1 */
#define LINKLOCAL_MULTICAST	"ff02::1"

static int mk_dst_addr(const char *ip, const char *iface,
		       struct sockaddr_in6 *dst)
{
	memset(dst, 0, sizeof(*dst));

	dst->sin6_family = AF_INET6;
	dst->sin6_port = htons(1025);

	if (inet_pton(AF_INET6, ip, &dst->sin6_addr) != 1) {
		log_err("Invalid IPv6: %s", ip);
		return -1;
	}

	dst->sin6_scope_id = if_nametoindex(iface);
	if (!dst->sin6_scope_id) {
		log_err("Failed to get index of iface: %s", iface);
		return -1;
	}

	return 0;
}

static int send_packet(const char *iface)
{
	struct sockaddr_in6 dst;
	char msg[] = "msg";
	int err = 0;
	int fd = -1;

	if (mk_dst_addr(LINKLOCAL_MULTICAST, iface, &dst))
		goto err;

	fd = socket(AF_INET6, SOCK_DGRAM, 0);
	if (fd == -1) {
		log_err("Failed to create UDP socket");
		goto err;
	}

	if (sendto(fd, &msg, sizeof(msg), 0, (const struct sockaddr *)&dst,
		   sizeof(dst)) == -1) {
		log_err("Failed to send datagram");
		goto err;
	}

	goto out;
err:
	err = -1;
out:
	if (fd >= 0)
		close(fd);
	return err;
}

int get_map_fd_by_prog_id(int prog_id)
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
	info.map_ids = (__u64) (unsigned long) map_ids;

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

int check_ancestor_cgroup_ids(int prog_id)
{
	__u64 actual_ids[NUM_CGROUP_LEVELS], expected_ids[NUM_CGROUP_LEVELS];
	__u32 level;
	int err = 0;
	int map_fd;

	expected_ids[0] = 0x100000001;	/* root cgroup */
	expected_ids[1] = get_cgroup_id("");
	expected_ids[2] = get_cgroup_id(CGROUP_PATH);
	expected_ids[3] = 0; /* non-existent cgroup */

	map_fd = get_map_fd_by_prog_id(prog_id);
	if (map_fd < 0)
		goto err;

	for (level = 0; level < NUM_CGROUP_LEVELS; ++level) {
		if (bpf_map_lookup_elem(map_fd, &level, &actual_ids[level])) {
			log_err("Failed to lookup key %d", level);
			goto err;
		}
		if (actual_ids[level] != expected_ids[level]) {
			log_err("%llx (actual) != %llx (expected), level: %u\n",
				actual_ids[level], expected_ids[level], level);
			goto err;
		}
	}

	goto out;
err:
	err = -1;
out:
	if (map_fd >= 0)
		close(map_fd);
	return err;
}

int main(int argc, char **argv)
{
	int cgfd = -1;
	int err = 0;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s iface prog_id\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (setup_cgroup_environment())
		goto err;

	cgfd = create_and_get_cgroup(CGROUP_PATH);
	if (cgfd < 0)
		goto err;

	if (join_cgroup(CGROUP_PATH))
		goto err;

	if (send_packet(argv[1]))
		goto err;

	if (check_ancestor_cgroup_ids(atoi(argv[2])))
		goto err;

	goto out;
err:
	err = -1;
out:
	close(cgfd);
	cleanup_cgroup_environment();
	printf("[%s]\n", err ? "FAIL" : "PASS");
	return err;
}
