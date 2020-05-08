// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/err.h>
#include <linux/in.h>
#include <linux/in6.h>

#include "network_helpers.h"

#define clean_errno() (errno == 0 ? "None" : strerror(errno))
#define log_err(MSG, ...) fprintf(stderr, "(%s:%d: errno: %s) " MSG "\n", \
	__FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)

struct ipv4_packet pkt_v4 = {
	.eth.h_proto = __bpf_constant_htons(ETH_P_IP),
	.iph.ihl = 5,
	.iph.protocol = IPPROTO_TCP,
	.iph.tot_len = __bpf_constant_htons(MAGIC_BYTES),
	.tcp.urg_ptr = 123,
	.tcp.doff = 5,
};

struct ipv6_packet pkt_v6 = {
	.eth.h_proto = __bpf_constant_htons(ETH_P_IPV6),
	.iph.nexthdr = IPPROTO_TCP,
	.iph.payload_len = __bpf_constant_htons(MAGIC_BYTES),
	.tcp.urg_ptr = 123,
	.tcp.doff = 5,
};

int start_server(int family, int type)
{
	struct sockaddr_storage addr = {};
	socklen_t len;
	int fd;

	if (family == AF_INET) {
		struct sockaddr_in *sin = (void *)&addr;

		sin->sin_family = AF_INET;
		len = sizeof(*sin);
	} else {
		struct sockaddr_in6 *sin6 = (void *)&addr;

		sin6->sin6_family = AF_INET6;
		len = sizeof(*sin6);
	}

	fd = socket(family, type | SOCK_NONBLOCK, 0);
	if (fd < 0) {
		log_err("Failed to create server socket");
		return -1;
	}

	if (bind(fd, (const struct sockaddr *)&addr, len) < 0) {
		log_err("Failed to bind socket");
		close(fd);
		return -1;
	}

	if (type == SOCK_STREAM) {
		if (listen(fd, 1) < 0) {
			log_err("Failed to listed on socket");
			close(fd);
			return -1;
		}
	}

	return fd;
}

static const struct timeval timeo_sec = { .tv_sec = 3 };
static const size_t timeo_optlen = sizeof(timeo_sec);

int connect_to_fd(int family, int type, int server_fd)
{
	struct sockaddr_storage addr;
	socklen_t len = sizeof(addr);
	int fd;

	fd = socket(family, type, 0);
	if (fd < 0) {
		log_err("Failed to create client socket");
		return -1;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeo_sec, timeo_optlen)) {
		log_err("Failed to set SO_RCVTIMEO");
		goto out;
	}

	if (getsockname(server_fd, (struct sockaddr *)&addr, &len)) {
		log_err("Failed to get server addr");
		goto out;
	}

	if (connect(fd, (const struct sockaddr *)&addr, len) < 0) {
		log_err("Fail to connect to server with family %d", family);
		goto out;
	}

	return fd;

out:
	close(fd);
	return -1;
}
