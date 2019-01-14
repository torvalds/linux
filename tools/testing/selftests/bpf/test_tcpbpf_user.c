// SPDX-License-Identifier: GPL-2.0
#include <inttypes.h>
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

#define EXPECT_EQ(expected, actual, fmt)			\
	do {							\
		if ((expected) != (actual)) {			\
			printf("  Value of: " #actual "\n"	\
			       "    Actual: %" fmt "\n"		\
			       "  Expected: %" fmt "\n",	\
			       (actual), (expected));		\
			goto err;				\
		}						\
	} while (0)

int verify_result(const struct tcpbpf_globals *result)
{
	__u32 expected_events;

	expected_events = ((1 << BPF_SOCK_OPS_TIMEOUT_INIT) |
			   (1 << BPF_SOCK_OPS_RWND_INIT) |
			   (1 << BPF_SOCK_OPS_TCP_CONNECT_CB) |
			   (1 << BPF_SOCK_OPS_ACTIVE_ESTABLISHED_CB) |
			   (1 << BPF_SOCK_OPS_PASSIVE_ESTABLISHED_CB) |
			   (1 << BPF_SOCK_OPS_NEEDS_ECN) |
			   (1 << BPF_SOCK_OPS_STATE_CB) |
			   (1 << BPF_SOCK_OPS_TCP_LISTEN_CB));

	EXPECT_EQ(expected_events, result->event_map, "#" PRIx32);
	EXPECT_EQ(501ULL, result->bytes_received, "llu");
	EXPECT_EQ(1002ULL, result->bytes_acked, "llu");
	EXPECT_EQ(1, result->data_segs_in, PRIu32);
	EXPECT_EQ(1, result->data_segs_out, PRIu32);
	EXPECT_EQ(0x80, result->bad_cb_test_rv, PRIu32);
	EXPECT_EQ(0, result->good_cb_test_rv, PRIu32);
	EXPECT_EQ(1, result->num_listen, PRIu32);

	return 0;
err:
	return -1;
}

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
	int error = EXIT_FAILURE;
	struct bpf_object *obj;
	int prog_fd, map_fd;
	int cg_fd = -1;
	__u32 key = 0;
	int rv;

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

	if (verify_result(&g)) {
		printf("FAILED: Wrong stats\n");
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
