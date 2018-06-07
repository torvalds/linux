/* Copyright (c) 2016 Sargun Dhillon <sargun@sargun.me>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <linux/bpf.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include "bpf_load.h"
#include <linux/bpf.h>
#include "cgroup_helpers.h"

#define CGROUP_PATH		"/my-cgroup"

int main(int argc, char **argv)
{
	pid_t remote_pid, local_pid = getpid();
	int cg2, idx = 0, rc = 0;
	char filename[256];

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	if (setup_cgroup_environment())
		goto err;

	cg2 = create_and_get_cgroup(CGROUP_PATH);

	if (!cg2)
		goto err;

	if (bpf_map_update_elem(map_fd[0], &idx, &cg2, BPF_ANY)) {
		log_err("Adding target cgroup to map");
		goto err;
	}

	if (join_cgroup(CGROUP_PATH))
		goto err;

	/*
	 * The installed helper program catched the sync call, and should
	 * write it to the map.
	 */

	sync();
	bpf_map_lookup_elem(map_fd[1], &idx, &remote_pid);

	if (local_pid != remote_pid) {
		fprintf(stderr,
			"BPF Helper didn't write correct PID to map, but: %d\n",
			remote_pid);
		goto err;
	}

	/* Verify the negative scenario; leave the cgroup */
	if (join_cgroup("/"))
		goto err;

	remote_pid = 0;
	bpf_map_update_elem(map_fd[1], &idx, &remote_pid, BPF_ANY);

	sync();
	bpf_map_lookup_elem(map_fd[1], &idx, &remote_pid);

	if (local_pid == remote_pid) {
		fprintf(stderr, "BPF cgroup negative test did not work\n");
		goto err;
	}

	goto out;
err:
	rc = 1;

out:
	close(cg2);
	cleanup_cgroup_environment();
	return rc;
}
