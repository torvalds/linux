// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <linux/bpf.h>
#include <sys/types.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "bpf_rlimit.h"
#include "bpf_util.h"
#include "cgroup_helpers.h"

#include "test_tcpbpf.h"

static int bpf_find_map(const char *test, struct bpf_object *obj,
			const char *name)
{
	struct bpf_map *map;

	map = bpf_object__find_map_by_name(obj, name);
	if (!map) {
		printf("%s:FAIL:map '%s' not found\n", test, name);
		return -1;
	}
	return bpf_map__fd(map);
}

int main(int argc, char **argv)
{
	const char *file = "test_tcpbpf_kern.o";
	struct tcpbpf_globals g = {0};
	const char *cg_path = "/foo";
	bool debug_flag = false;
	int error = EXIT_FAILURE;
	struct bpf_object *obj;
	int prog_fd, map_fd;
	int cg_fd = -1;
	__u32 key = 0;
	int rv;

	if (argc > 1 && strcmp(argv[1], "-d") == 0)
		debug_flag = true;

	if (setup_cgroup_environment())
		goto err;

	cg_fd = create_and_get_cgroup(cg_path);
	if (!cg_fd)
		goto err;

	if (join_cgroup(cg_path))
		goto err;

	if (bpf_prog_load(file, BPF_PROG_TYPE_SOCK_OPS, &obj, &prog_fd)) {
		printf("FAILED: load_bpf_file failed for: %s\n", file);
		goto err;
	}

	rv = bpf_prog_attach(prog_fd, cg_fd, BPF_CGROUP_SOCK_OPS, 0);
	if (rv) {
		printf("FAILED: bpf_prog_attach: %d (%s)\n",
		       error, strerror(errno));
		goto err;
	}

	if (system("./tcp_server.py")) {
		printf("FAILED: TCP server\n");
		goto err;
	}

	map_fd = bpf_find_map(__func__, obj, "global_map");
	if (map_fd < 0)
		goto err;

	rv = bpf_map_lookup_elem(map_fd, &key, &g);
	if (rv != 0) {
		printf("FAILED: bpf_map_lookup_elem returns %d\n", rv);
		goto err;
	}

	if (g.bytes_received != 501 || g.bytes_acked != 1002 ||
	    g.data_segs_in != 1 || g.data_segs_out != 1 ||
	    (g.event_map ^ 0x47e) != 0 || g.bad_cb_test_rv != 0x80 ||
		g.good_cb_test_rv != 0) {
		printf("FAILED: Wrong stats\n");
		if (debug_flag) {
			printf("\n");
			printf("bytes_received: %d (expecting 501)\n",
			       (int)g.bytes_received);
			printf("bytes_acked:    %d (expecting 1002)\n",
			       (int)g.bytes_acked);
			printf("data_segs_in:   %d (expecting 1)\n",
			       g.data_segs_in);
			printf("data_segs_out:  %d (expecting 1)\n",
			       g.data_segs_out);
			printf("event_map:      0x%x (at least 0x47e)\n",
			       g.event_map);
			printf("bad_cb_test_rv: 0x%x (expecting 0x80)\n",
			       g.bad_cb_test_rv);
			printf("good_cb_test_rv:0x%x (expecting 0)\n",
			       g.good_cb_test_rv);
		}
		goto err;
	}
	printf("PASSED!\n");
	error = 0;
err:
	bpf_prog_detach(cg_fd, BPF_CGROUP_SOCK_OPS);
	close(cg_fd);
	cleanup_cgroup_environment();
	return error;
}
