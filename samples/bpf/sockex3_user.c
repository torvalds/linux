// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <assert.h>
#include <linux/bpf.h>
#include "libbpf.h"
#include "bpf_load.h"
#include "sock_example.h"
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/resource.h>

#define PARSE_IP 3
#define PARSE_IP_PROG_FD (prog_fd[0])
#define PROG_ARRAY_FD (map_fd[0])

struct bpf_flow_keys {
	__be32 src;
	__be32 dst;
	union {
		__be32 ports;
		__be16 port16[2];
	};
	__u32 ip_proto;
};

struct pair {
	__u64 packets;
	__u64 bytes;
};

int main(int argc, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	char filename[256];
	FILE *f;
	int i, sock, err, id, key = PARSE_IP;
	struct bpf_prog_info info = {};
	uint32_t info_len = sizeof(info);

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	setrlimit(RLIMIT_MEMLOCK, &r);

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	/* Test fd array lookup which returns the id of the bpf_prog */
	err = bpf_obj_get_info_by_fd(PARSE_IP_PROG_FD, &info, &info_len);
	assert(!err);
	err = bpf_map_lookup_elem(PROG_ARRAY_FD, &key, &id);
	assert(!err);
	assert(id == info.id);

	sock = open_raw_sock("lo");

	assert(setsockopt(sock, SOL_SOCKET, SO_ATTACH_BPF, &prog_fd[4],
			  sizeof(__u32)) == 0);

	if (argc > 1)
		f = popen("ping -c5 localhost", "r");
	else
		f = popen("netperf -l 4 localhost", "r");
	(void) f;

	for (i = 0; i < 5; i++) {
		struct bpf_flow_keys key = {}, next_key;
		struct pair value;

		sleep(1);
		printf("IP     src.port -> dst.port               bytes      packets\n");
		while (bpf_map_get_next_key(map_fd[2], &key, &next_key) == 0) {
			bpf_map_lookup_elem(map_fd[2], &next_key, &value);
			printf("%s.%05d -> %s.%05d %12lld %12lld\n",
			       inet_ntoa((struct in_addr){htonl(next_key.src)}),
			       next_key.port16[0],
			       inet_ntoa((struct in_addr){htonl(next_key.dst)}),
			       next_key.port16[1],
			       value.bytes, value.packets);
			key = next_key;
		}
	}
	return 0;
}
