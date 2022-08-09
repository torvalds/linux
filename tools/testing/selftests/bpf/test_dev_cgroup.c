// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2017 Facebook
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/time.h>

#include <linux/bpf.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#include "cgroup_helpers.h"
#include "testing_helpers.h"
#include "bpf_rlimit.h"

#define DEV_CGROUP_PROG "./dev_cgroup.o"

#define TEST_CGROUP "/test-bpf-based-device-cgroup/"

int main(int argc, char **argv)
{
	struct bpf_object *obj;
	int error = EXIT_FAILURE;
	int prog_fd, cgroup_fd;
	__u32 prog_cnt;

	if (bpf_prog_test_load(DEV_CGROUP_PROG, BPF_PROG_TYPE_CGROUP_DEVICE,
			  &obj, &prog_fd)) {
		printf("Failed to load DEV_CGROUP program\n");
		goto out;
	}

	cgroup_fd = cgroup_setup_and_join(TEST_CGROUP);
	if (cgroup_fd < 0) {
		printf("Failed to create test cgroup\n");
		goto out;
	}

	/* Attach bpf program */
	if (bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_DEVICE, 0)) {
		printf("Failed to attach DEV_CGROUP program");
		goto err;
	}

	if (bpf_prog_query(cgroup_fd, BPF_CGROUP_DEVICE, 0, NULL, NULL,
			   &prog_cnt)) {
		printf("Failed to query attached programs");
		goto err;
	}

	/* All operations with /dev/zero and and /dev/urandom are allowed,
	 * everything else is forbidden.
	 */
	assert(system("rm -f /tmp/test_dev_cgroup_null") == 0);
	assert(system("mknod /tmp/test_dev_cgroup_null c 1 3"));
	assert(system("rm -f /tmp/test_dev_cgroup_null") == 0);

	/* /dev/zero is whitelisted */
	assert(system("rm -f /tmp/test_dev_cgroup_zero") == 0);
	assert(system("mknod /tmp/test_dev_cgroup_zero c 1 5") == 0);
	assert(system("rm -f /tmp/test_dev_cgroup_zero") == 0);

	assert(system("dd if=/dev/urandom of=/dev/zero count=64") == 0);

	/* src is allowed, target is forbidden */
	assert(system("dd if=/dev/urandom of=/dev/full count=64"));

	/* src is forbidden, target is allowed */
	assert(system("dd if=/dev/random of=/dev/zero count=64"));

	error = 0;
	printf("test_dev_cgroup:PASS\n");

err:
	cleanup_cgroup_environment();

out:
	return error;
}
