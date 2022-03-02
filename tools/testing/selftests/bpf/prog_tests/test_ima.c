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

#define MAX_SAMPLES 2

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

static u64 ima_hash_from_bpf[MAX_SAMPLES];
static int ima_hash_from_bpf_idx;

static int process_sample(void *ctx, void *data, size_t len)
{
	if (ima_hash_from_bpf_idx >= MAX_SAMPLES)
		return -ENOSPC;

	ima_hash_from_bpf[ima_hash_from_bpf_idx++] = *((u64 *)data);
	return 0;
}

static void test_init(struct ima__bss *bss)
{
	ima_hash_from_bpf_idx = 0;

	bss->use_ima_file_hash = false;
}

void test_test_ima(void)
{
	char measured_dir_template[] = "/tmp/ima_measuredXXXXXX";
	struct ring_buffer *ringbuf = NULL;
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

	/*
	 * Test #1
	 * - Goal: obtain a sample with the bpf_ima_inode_hash() helper
	 * - Expected result:  1 sample (/bin/true)
	 */
	test_init(skel->bss);
	err = run_measured_process(measured_dir, &skel->bss->monitored_pid);
	if (CHECK(err, "run_measured_process #1", "err = %d\n", err))
		goto close_clean;

	err = ring_buffer__consume(ringbuf);
	ASSERT_EQ(err, 1, "num_samples_or_err");
	ASSERT_NEQ(ima_hash_from_bpf[0], 0, "ima_hash");

	/*
	 * Test #2
	 * - Goal: obtain samples with the bpf_ima_file_hash() helper
	 * - Expected result: 2 samples (./ima_setup.sh, /bin/true)
	 */
	test_init(skel->bss);
	skel->bss->use_ima_file_hash = true;
	err = run_measured_process(measured_dir, &skel->bss->monitored_pid);
	if (CHECK(err, "run_measured_process #2", "err = %d\n", err))
		goto close_clean;

	err = ring_buffer__consume(ringbuf);
	ASSERT_EQ(err, 2, "num_samples_or_err");
	ASSERT_NEQ(ima_hash_from_bpf[0], 0, "ima_hash");
	ASSERT_NEQ(ima_hash_from_bpf[1], 0, "ima_hash");

close_clean:
	snprintf(cmd, sizeof(cmd), "./ima_setup.sh cleanup %s", measured_dir);
	err = system(cmd);
	CHECK(err, "failed to run command", "%s, errno = %d\n", cmd, errno);
close_prog:
	ring_buffer__free(ringbuf);
	ima__destroy(skel);
}
