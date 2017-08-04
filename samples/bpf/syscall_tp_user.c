/* Copyright (c) 2017 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <linux/bpf.h>
#include <string.h>
#include <linux/perf_event.h>
#include <errno.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/resource.h>
#include "libbpf.h"
#include "bpf_load.h"

/* This program verifies bpf attachment to tracepoint sys_enter_* and sys_exit_*.
 * This requires kernel CONFIG_FTRACE_SYSCALLS to be set.
 */

static void verify_map(int map_id)
{
	__u32 key = 0;
	__u32 val;

	if (bpf_map_lookup_elem(map_id, &key, &val) != 0) {
		fprintf(stderr, "map_lookup failed: %s\n", strerror(errno));
		return;
	}
	if (val == 0)
		fprintf(stderr, "failed: map #%d returns value 0\n", map_id);
}

int main(int argc, char **argv)
{
	struct rlimit r = {RLIM_INFINITY, RLIM_INFINITY};
	char filename[256];
	int fd;

	setrlimit(RLIMIT_MEMLOCK, &r);
	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);

	if (load_bpf_file(filename)) {
		fprintf(stderr, "%s", bpf_log_buf);
		return 1;
	}

	/* current load_bpf_file has perf_event_open default pid = -1
	 * and cpu = 0, which permits attached bpf execution on
	 * all cpus for all pid's. bpf program execution ignores
	 * cpu affinity.
	 */
	/* trigger some "open" operations */
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "open failed: %s\n", strerror(errno));
		return 1;
	}
	close(fd);

	/* verify the map */
	verify_map(map_fd[0]);
	verify_map(map_fd[1]);

	return 0;
}
