// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017 Facebook
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
#include <bpf/libbpf.h>

static int map_fd[7];

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
	struct bpf_link *link = NULL;
	struct bpf_program *prog;
	struct bpf_object *obj;
	char filename[256];

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	obj = bpf_object__open_file(filename, NULL);
	if (libbpf_get_error(obj)) {
		fprintf(stderr, "ERROR: opening BPF object file failed\n");
		return 0;
	}

	prog = bpf_object__find_program_by_name(obj, "trace_sys_connect");
	if (!prog) {
		printf("finding a prog in obj file failed\n");
		goto cleanup;
	}

	/* load BPF program */
	if (bpf_object__load(obj)) {
		fprintf(stderr, "ERROR: loading BPF object file failed\n");
		goto cleanup;
	}

	map_fd[0] = bpf_object__find_map_fd_by_name(obj, "port_a");
	map_fd[1] = bpf_object__find_map_fd_by_name(obj, "port_h");
	map_fd[2] = bpf_object__find_map_fd_by_name(obj, "reg_result_h");
	map_fd[3] = bpf_object__find_map_fd_by_name(obj, "inline_result_h");
	map_fd[4] = bpf_object__find_map_fd_by_name(obj, "a_of_port_a");
	map_fd[5] = bpf_object__find_map_fd_by_name(obj, "h_of_port_a");
	map_fd[6] = bpf_object__find_map_fd_by_name(obj, "h_of_port_h");
	if (map_fd[0] < 0 || map_fd[1] < 0 || map_fd[2] < 0 ||
	    map_fd[3] < 0 || map_fd[4] < 0 || map_fd[5] < 0 || map_fd[6] < 0) {
		fprintf(stderr, "ERROR: finding a map in obj file failed\n");
		goto cleanup;
	}

	link = bpf_program__attach(prog);
	if (libbpf_get_error(link)) {
		fprintf(stderr, "ERROR: bpf_program__attach failed\n");
		link = NULL;
		goto cleanup;
	}

	test_map_in_map();

cleanup:
	bpf_link__destroy(link);
	bpf_object__close(obj);
	return 0;
}
