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
#include "libbpf.h"
#include "bpf_load.h"
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/bpf.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/limits.h>

#define CGROUP_MOUNT_PATH	"/mnt"
#define CGROUP_PATH		"/mnt/my-cgroup"

#define clean_errno() (errno == 0 ? "None" : strerror(errno))
#define log_err(MSG, ...) fprintf(stderr, "(%s:%d: errno: %s) " MSG "\n", \
	__FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)

static int join_cgroup(char *path)
{
	int fd, rc = 0;
	pid_t pid = getpid();
	char cgroup_path[PATH_MAX + 1];

	snprintf(cgroup_path, sizeof(cgroup_path), "%s/cgroup.procs", path);

	fd = open(cgroup_path, O_WRONLY);
	if (fd < 0) {
		log_err("Opening Cgroup");
		return 1;
	}

	if (dprintf(fd, "%d\n", pid) < 0) {
		log_err("Joining Cgroup");
		rc = 1;
	}
	close(fd);
	return rc;
}

int main(int argc, char **argv)
{
	char filename[256];
	int cg2, idx = 0;
	pid_t remote_pid, local_pid = getpid();

	snprintf(filename, sizeof(filename), "%s_kern.o", argv[0]);
	if (load_bpf_file(filename)) {
		printf("%s", bpf_log_buf);
		return 1;
	}

	/*
	 * This is to avoid interfering with existing cgroups. Unfortunately,
	 * most people don't have cgroupv2 enabled at this point in time.
	 * It's easier to create our own mount namespace and manage it
	 * ourselves.
	 */
	if (unshare(CLONE_NEWNS)) {
		log_err("unshare");
		return 1;
	}

	if (mount("none", "/", NULL, MS_REC | MS_PRIVATE, NULL)) {
		log_err("mount fakeroot");
		return 1;
	}

	if (mount("none", CGROUP_MOUNT_PATH, "cgroup2", 0, NULL)) {
		log_err("mount cgroup2");
		return 1;
	}

	if (mkdir(CGROUP_PATH, 0777) && errno != EEXIST) {
		log_err("mkdir cgroup");
		return 1;
	}

	cg2 = open(CGROUP_PATH, O_RDONLY);
	if (cg2 < 0) {
		log_err("opening target cgroup");
		goto cleanup_cgroup_err;
	}

	if (bpf_update_elem(map_fd[0], &idx, &cg2, BPF_ANY)) {
		log_err("Adding target cgroup to map");
		goto cleanup_cgroup_err;
	}
	if (join_cgroup("/mnt/my-cgroup")) {
		log_err("Leaving target cgroup");
		goto cleanup_cgroup_err;
	}

	/*
	 * The installed helper program catched the sync call, and should
	 * write it to the map.
	 */

	sync();
	bpf_lookup_elem(map_fd[1], &idx, &remote_pid);

	if (local_pid != remote_pid) {
		fprintf(stderr,
			"BPF Helper didn't write correct PID to map, but: %d\n",
			remote_pid);
		goto leave_cgroup_err;
	}

	/* Verify the negative scenario; leave the cgroup */
	if (join_cgroup(CGROUP_MOUNT_PATH))
		goto leave_cgroup_err;

	remote_pid = 0;
	bpf_update_elem(map_fd[1], &idx, &remote_pid, BPF_ANY);

	sync();
	bpf_lookup_elem(map_fd[1], &idx, &remote_pid);

	if (local_pid == remote_pid) {
		fprintf(stderr, "BPF cgroup negative test did not work\n");
		goto cleanup_cgroup_err;
	}

	rmdir(CGROUP_PATH);
	return 0;

	/* Error condition, cleanup */
leave_cgroup_err:
	join_cgroup(CGROUP_MOUNT_PATH);
cleanup_cgroup_err:
	rmdir(CGROUP_PATH);
	return 1;
}
