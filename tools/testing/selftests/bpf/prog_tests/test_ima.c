// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2020 Google LLC.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <test_progs.h>
#include <linux/ring_buffer.h>

#include "ima.skel.h"

static int run_measured_process(const char *measured_dir, u32 *monitored_pid)
{
	int child_pid, child_status;

	child_pid = fork();
	if (child_pid == 0) {
		*monitored_pid = getpid();
		execlp("./ima_setup.sh", "./ima_setup.sh", "run", measured_dir,
		       NULL);
		exit(errno);

	} else if (child_pid > 0) {
		waitpid(child_pid, &child_status, 0);
		return WEXITSTATUS(child_status);
	}

	return -EINVAL;
}

static u64 ima_hash_from_bpf;

static int process_sample(void *ctx, void *data, size_t len)
{
	ima_hash_from_bpf = *((u64 *)data);
	return 0;
}

void test_test_ima(void)
{
	char measured_dir_template[] = "/tmp/ima_measuredXXXXXX";
	struct ring_buffer *ringbuf;
	const char *measured_dir;
	char cmd[256];

	int err, duration = 0;
	struct ima *skel = NULL;

	skel = ima__open_and_load();
	if (CHECK(!skel, "skel_load", "skeleton failed\n"))
		goto close_prog;

	ringbuf = ring_buffer__new(bpf_map__fd(skel->maps.ringbuf),
				   process_sample, NULL, NULL);
	if (!ASSERT_OK_PTR(ringbuf, "ringbuf"))
		goto close_prog;

	err = ima__attach(skel);
	if (CHECK(err, "attach", "attach failed: %d\n", err))
		goto close_prog;

	measured_dir = mkdtemp(measured_dir_template);
	if (CHECK(measured_dir == NULL, "mkdtemp", "err %d\n", errno))
		goto close_prog;

	snprintf(cmd, sizeof(cmd), "./ima_setup.sh setup %s", measured_dir);
	err = system(cmd);
	if (CHECK(err, "failed to run command", "%s, errno = %d\n", cmd, errno))
		goto close_clean;

	err = run_measured_process(measured_dir, &skel->bss->monitored_pid);
	if (CHECK(err, "run_measured_process", "err = %d\n", err))
		goto close_clean;

	err = ring_buffer__consume(ringbuf);
	ASSERT_EQ(err, 1, "num_samples_or_err");
	ASSERT_NEQ(ima_hash_from_bpf, 0, "ima_hash");

close_clean:
	snprintf(cmd, sizeof(cmd), "./ima_setup.sh cleanup %s", measured_dir);
	err = system(cmd);
	CHECK(err, "failed to run command", "%s, errno = %d\n", cmd, errno);
close_prog:
	ima__destroy(skel);
}
