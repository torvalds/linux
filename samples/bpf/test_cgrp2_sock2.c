// SPDX-License-Identifier: GPL-2.0
/* eBPF example program:
 *
 * - Loads eBPF program
 *
 *   The eBPF program loads a filter from file and attaches the
 *   program to a cgroup using BPF_PROG_ATTACH
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <linux/bpf.h>

#include "libbpf.h"
#include "bpf_load.h"

static int usage(const char *argv0)
{
	printf("Usage: %s cg-path filter-path [filter-id]\n", argv0);
	return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
	int cg_fd, ret, filter_id = 0;

	if (argc < 3)
		return usage(argv[0]);

	cg_fd = open(argv[1], O_DIRECTORY | O_RDONLY);
	if (cg_fd < 0) {
		printf("Failed to open cgroup path: '%s'\n", strerror(errno));
		return EXIT_FAILURE;
	}

	if (load_bpf_file(argv[2]))
		return EXIT_FAILURE;

	printf("Output from kernel verifier:\n%s\n-------\n", bpf_log_buf);

	if (argc > 3)
		filter_id = atoi(argv[3]);

	if (filter_id > prog_cnt) {
		printf("Invalid program id; program not found in file\n");
		return EXIT_FAILURE;
	}

	ret = bpf_prog_attach(prog_fd[filter_id], cg_fd,
			      BPF_CGROUP_INET_SOCK_CREATE, 0);
	if (ret < 0) {
		printf("Failed to attach prog to cgroup: '%s'\n",
		       strerror(errno));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
