// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Cloudflare

#include "test_progs.h"

#define TCP_REPAIR		19	/* TCP sock is under repair right now */

#define TCP_REPAIR_ON		1
#define TCP_REPAIR_OFF_NO_WP	-1	/* Turn off without window probes */

static int connected_socket_v4(void)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(80),
		.sin_addr = { inet_addr("127.0.0.1") },
	};
	socklen_t len = sizeof(addr);
	int s, repair, err;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (CHECK_FAIL(s == -1))
		goto error;

	repair = TCP_REPAIR_ON;
	err = setsockopt(s, SOL_TCP, TCP_REPAIR, &repair, sizeof(repair));
	if (CHECK_FAIL(err))
		goto error;

	err = connect(s, (struct sockaddr *)&addr, len);
	if (CHECK_FAIL(err))
		goto error;

	repair = TCP_REPAIR_OFF_NO_WP;
	err = setsockopt(s, SOL_TCP, TCP_REPAIR, &repair, sizeof(repair));
	if (CHECK_FAIL(err))
		goto error;

	return s;
error:
	perror(__func__);
	close(s);
	return -1;
}

/* Create a map, populate it with one socket, and free the map. */
static void test_sockmap_create_update_free(enum bpf_map_type map_type)
{
	const int zero = 0;
	int s, map, err;

	s = connected_socket_v4();
	if (CHECK_FAIL(s == -1))
		return;

	map = bpf_create_map(map_type, sizeof(int), sizeof(int), 1, 0);
	if (CHECK_FAIL(map == -1)) {
		perror("bpf_create_map");
		goto out;
	}

	err = bpf_map_update_elem(map, &zero, &s, BPF_NOEXIST);
	if (CHECK_FAIL(err)) {
		perror("bpf_map_update");
		goto out;
	}

out:
	close(map);
	close(s);
}

void test_sockmap_basic(void)
{
	if (test__start_subtest("sockmap create_update_free"))
		test_sockmap_create_update_free(BPF_MAP_TYPE_SOCKMAP);
	if (test__start_subtest("sockhash create_update_free"))
		test_sockmap_create_update_free(BPF_MAP_TYPE_SOCKHASH);
}
