// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <assert.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>
#include "sock_example.h"
#include <unistd.h>
#include <arpa/inet.h>

struct flow_key_record {
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
	int i, sock, fd, main_prog_fd, hash_map_fd;
	struct bpf_program *prog;
	struct bpf_object *obj;
	char filename[256];
	FILE *f;

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	obj = bpf_object__open_file(filename, NULL);
	if (libbpf_get_error(obj)) {
		fprintf(stderr, "ERROR: opening BPF object file failed\n");
		return 0;
	}

	/* load BPF program */
	if (bpf_object__load(obj)) {
		fprintf(stderr, "ERROR: loading BPF object file failed\n");
		goto cleanup;
	}

	hash_map_fd = bpf_object__find_map_fd_by_name(obj, "hash_map");
	if (hash_map_fd < 0) {
		fprintf(stderr, "ERROR: finding a map in obj file failed\n");
		goto cleanup;
	}

	/* find BPF main program */
	main_prog_fd = 0;
	bpf_object__for_each_program(prog, obj) {
		fd = bpf_program__fd(prog);

		if (!strcmp(bpf_program__name(prog), "main_prog"))
			main_prog_fd = fd;
	}

	if (main_prog_fd == 0) {
		fprintf(stderr, "ERROR: can't find main_prog\n");
		goto cleanup;
	}

	sock = open_raw_sock("lo");

	/* attach BPF program to socket */
	assert(setsockopt(sock, SOL_SOCKET, SO_ATTACH_BPF, &main_prog_fd,
			  sizeof(__u32)) == 0);

	if (argc > 1)
		f = popen("ping -4 -c5 localhost", "r");
	else
		f = popen("netperf -l 4 localhost", "r");
	(void) f;

	for (i = 0; i < 5; i++) {
		struct flow_key_record key = {}, next_key;
		struct pair value;

		sleep(1);
		printf("IP     src.port -> dst.port               bytes      packets\n");
		while (bpf_map_get_next_key(hash_map_fd, &key, &next_key) == 0) {
			bpf_map_lookup_elem(hash_map_fd, &next_key, &value);
			printf("%s.%05d -> %s.%05d %12lld %12lld\n",
			       inet_ntoa((struct in_addr){htonl(next_key.src)}),
			       next_key.port16[0],
			       inet_ntoa((struct in_addr){htonl(next_key.dst)}),
			       next_key.port16[1],
			       value.bytes, value.packets);
			key = next_key;
		}
	}

cleanup:
	bpf_object__close(obj);
	return 0;
}
