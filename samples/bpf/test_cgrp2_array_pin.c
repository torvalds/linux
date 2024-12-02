// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016 Facebook
 */
#include <linux/unistd.h>
#include <linux/bpf.h>

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <bpf/bpf.h>

static void usage(void)
{
	printf("Usage: test_cgrp2_array_pin [...]\n");
	printf("       -F <file>   File to pin an BPF cgroup array\n");
	printf("       -U <file>   Update an already pinned BPF cgroup array\n");
	printf("       -v <value>  Full path of the cgroup2\n");
	printf("       -h          Display this help\n");
}

int main(int argc, char **argv)
{
	const char *pinned_file = NULL, *cg2 = NULL;
	int create_array = 1;
	int array_key = 0;
	int array_fd = -1;
	int cg2_fd = -1;
	int ret = -1;
	int opt;

	while ((opt = getopt(argc, argv, "F:U:v:")) != -1) {
		switch (opt) {
		/* General args */
		case 'F':
			pinned_file = optarg;
			break;
		case 'U':
			pinned_file = optarg;
			create_array = 0;
			break;
		case 'v':
			cg2 = optarg;
			break;
		default:
			usage();
			goto out;
		}
	}

	if (!cg2 || !pinned_file) {
		usage();
		goto out;
	}

	cg2_fd = open(cg2, O_RDONLY);
	if (cg2_fd < 0) {
		fprintf(stderr, "open(%s,...): %s(%d)\n",
			cg2, strerror(errno), errno);
		goto out;
	}

	if (create_array) {
		array_fd = bpf_map_create(BPF_MAP_TYPE_CGROUP_ARRAY, NULL,
					  sizeof(uint32_t), sizeof(uint32_t),
					  1, NULL);
		if (array_fd < 0) {
			fprintf(stderr,
				"bpf_create_map(BPF_MAP_TYPE_CGROUP_ARRAY,...): %s(%d)\n",
				strerror(errno), errno);
			goto out;
		}
	} else {
		array_fd = bpf_obj_get(pinned_file);
		if (array_fd < 0) {
			fprintf(stderr, "bpf_obj_get(%s): %s(%d)\n",
				pinned_file, strerror(errno), errno);
			goto out;
		}
	}

	ret = bpf_map_update_elem(array_fd, &array_key, &cg2_fd, 0);
	if (ret) {
		perror("bpf_map_update_elem");
		goto out;
	}

	if (create_array) {
		ret = bpf_obj_pin(array_fd, pinned_file);
		if (ret) {
			fprintf(stderr, "bpf_obj_pin(..., %s): %s(%d)\n",
				pinned_file, strerror(errno), errno);
			goto out;
		}
	}

out:
	if (array_fd != -1)
		close(array_fd);
	if (cg2_fd != -1)
		close(cg2_fd);
	return ret;
}
