/*
 * Copyright (c) 2017 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <sys/resource.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <bpf/bpf.h>
#include "bpf_load.h"

#define PORT_A		(map_fd[0])
#define PORT_H		(map_fd[1])
#define REG_RESULT_H	(map_fd[2])
#define INLINE_RESULT_H	(map_fd[3])
#define A_OF_PORT_A	(map_fd[4]) /* Test case #0 */
#define H_OF_PORT_A	(map_fd[5]) /* Test case #1 */
#define H_OF_PORT_H	(map_fd[6]) /* Test case #2 */

static const char * const test_names[] = {
	"Array of Array",
	"Hash of Array",
	"Hash of Hash",
};

#define NR_TESTS (sizeof(test_names) / sizeof(*test_names))

static void check_map_id(int inner_map_fd, int map_in_map_fd, uint32_t key)
{
	struct bpf_map_info info = {};
	uint32_t info_len = sizeof(info);
	int ret, id;

	ret = bpf_obj_get_info_by_fd(inner_map_fd, &info, &info_len);
	assert(!ret);

	ret = bpf_map_lookup_elem(map_in_map_fd, &key, &id);
	assert(!ret);
	assert(id == info.id);
}

static void populate_map(uint32_t port_key, int magic_result)
{
	int ret;

	ret = bpf_map_update_elem(PORT_A, &port_key, &magic_result, BPF_ANY);
	assert(!ret);

	ret = bpf_map_update_elem(PORT_H, &port_key, &magic_result,
				  BPF_NOEXIST);
	assert(!ret);

	ret = bpf_map_update_elem(A_OF_PORT_A, &port_key, &PORT_A, BPF_ANY);
	assert(!ret);
	check_map_id(PORT_A, A_OF_PORT_A, port_key);

	ret = bpf_map_update_elem(H_OF_PORT_A, &port_key, &PORT_A, BPF_NOEXIST);
	assert(!ret);
	check_map_id(PORT_A, H_OF_PORT_A, port_key);

	ret = bpf_map_update_elem(H_OF_PORT_H, &port_key, &PORT_H, BPF_NOEXIST);
	assert(!ret);
	check_map_id(PORT_H, H_OF_PORT_H, port_key);
}

static void test_map_in_map(void)
{
	struct sockaddr_in6 in6 = { .sin6_family = AF_INET6 };
	uint32_t result_key = 0, port_key;
	int result, inline_result;
	int magic_result = 0xfaceb00c;
	int ret;
	int i;

	port_key = rand() & 0x00FF;
	populate_map(port_key, magic_result);

	in6.sin6_addr.s6_addr16[0] = 0xdead;
	in6.sin6_addr.s6_addr16[1] = 0xbeef;
	in6.sin6_port = port_key;

	for (i = 0; i < NR_TESTS; i++) {
		printf("%s: ", test_names[i]);

		in6.sin6_addr.s6_addr16[7] = i;
		ret = connect(-1, (struct sockaddr *)&in6, sizeof(in6));
		assert(ret == -1 && errno == EBADF);

		ret = bpf_map_lookup_elem(REG_RESULT_H, &result_key, &result);
		assert(!ret);

		ret = bpf_map_lookup_elem(INLINE_RESULT_H, &result_key,
					  &inline_result);
		assert(!ret);

		if (result != magic_result || inline_result != magic_result) {
			printf("Error. result:%d inline_result:%d\n",
			       result, inline_result);
			exit(1);
		}

		bpf_map_delete_elem(REG_RESULT_H, &result_key);
		bpf_map_delete_elem(INLINE_RESULT_H, &result_key);

		printf("Pass\n");
	}
}

int main(int argc, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	char filename[256];

	assert(!setrlimit(RLIMIT_MEMLOCK, &r));

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	test_map_in_map();

	return 0;
}
