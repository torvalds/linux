// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Christian Brauner <brauner@kernel.org> */

/*
 * Test BPF LSM block device integrity hooks with dm-verity.
 *
 * Creates a dm-verity device over loopback, which triggers
 * security_bdev_setintegrity() during verity_preresume().
 * Verifies that the BPF program correctly tracks the integrity
 * metadata in its hashmap.
 */

#define _GNU_SOURCE
#include <test_progs.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "lsm_bdev.skel.h"

/* Must match the definition in progs/lsm_bdev.c. */
struct verity_info {
	__u8  has_roothash;
	__u8  sig_valid;
	__u32 setintegrity_cnt;
};

#define DATA_SIZE_MB	8
#define HASH_SIZE_MB	1
#define DM_NAME		"bpf_test_verity"
#define DM_DEV_PATH	"/dev/mapper/" DM_NAME

/* Run a command and optionally capture the first line of stdout. */
static int run_cmd(const char *cmd, char *out, size_t out_sz)
{
	FILE *fp;
	int ret;

	fp = popen(cmd, "r");
	if (!fp)
		return -1;

	if (out && out_sz > 0) {
		if (!fgets(out, out_sz, fp))
			out[0] = '\0';
		/* strip trailing newline */
		out[strcspn(out, "\n")] = '\0';
	}

	ret = pclose(fp);
	return WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
}

static bool has_prerequisites(void)
{
	if (getuid() != 0) {
		printf("SKIP: must be root\n");
		return false;
	}

	if (run_cmd("modprobe loop 2>/dev/null", NULL, 0) &&
	    run_cmd("ls /dev/loop-control 2>/dev/null", NULL, 0)) {
		printf("SKIP: no loop device support\n");
		return false;
	}

	if (run_cmd("modprobe dm-verity 2>/dev/null", NULL, 0) &&
	    run_cmd("dmsetup targets 2>/dev/null | grep -q verity", NULL, 0)) {
		printf("SKIP: dm-verity module not available\n");
		return false;
	}

	if (run_cmd("which veritysetup >/dev/null 2>&1", NULL, 0)) {
		printf("SKIP: veritysetup not found\n");
		return false;
	}

	return true;
}

void test_lsm_bdev(void)
{
	char data_img[] = "/tmp/bpf_verity_data_XXXXXX";
	char hash_img[] = "/tmp/bpf_verity_hash_XXXXXX";
	char data_loop[64] = {};
	char hash_loop[64] = {};
	char roothash[256] = {};
	char cmd[512];
	int data_fd = -1, hash_fd = -1;
	struct lsm_bdev *skel = NULL;
	struct verity_info val;
	struct stat st;
	__u32 dev_key;
	int err;

	if (!has_prerequisites()) {
		test__skip();
		return;
	}

	/* Clean up any stale device from a previous crashed run. */
	snprintf(cmd, sizeof(cmd), "dmsetup remove %s 2>/dev/null", DM_NAME);
	run_cmd(cmd, NULL, 0);

	/* Create temporary image files. */
	data_fd = mkstemp(data_img);
	if (!ASSERT_OK_FD(data_fd, "mkstemp data"))
		return;

	hash_fd = mkstemp(hash_img);
	if (!ASSERT_OK_FD(hash_fd, "mkstemp hash"))
		goto cleanup;

	if (!ASSERT_OK(ftruncate(data_fd, DATA_SIZE_MB * 1024 * 1024),
		       "truncate data"))
		goto cleanup;

	if (!ASSERT_OK(ftruncate(hash_fd, HASH_SIZE_MB * 1024 * 1024),
		       "truncate hash"))
		goto cleanup;

	close(data_fd);
	data_fd = -1;
	close(hash_fd);
	hash_fd = -1;

	/* Set up loop devices. */
	snprintf(cmd, sizeof(cmd),
		 "losetup --find --show %s 2>/dev/null", data_img);
	if (!ASSERT_OK(run_cmd(cmd, data_loop, sizeof(data_loop)),
		       "losetup data"))
		goto teardown;

	snprintf(cmd, sizeof(cmd),
		 "losetup --find --show %s 2>/dev/null", hash_img);
	if (!ASSERT_OK(run_cmd(cmd, hash_loop, sizeof(hash_loop)),
		       "losetup hash"))
		goto teardown;

	/* Format the dm-verity device and capture the root hash. */
	snprintf(cmd, sizeof(cmd),
		 "veritysetup format %s %s 2>/dev/null | "
		 "grep -i 'root hash' | awk '{print $NF}'",
		 data_loop, hash_loop);
	if (!ASSERT_OK(run_cmd(cmd, roothash, sizeof(roothash)),
		       "veritysetup format"))
		goto teardown;

	if (!ASSERT_GT((int)strlen(roothash), 0, "roothash not empty"))
		goto teardown;

	/* Load and attach BPF program before activating dm-verity. */
	skel = lsm_bdev__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel open_and_load"))
		goto teardown;

	err = lsm_bdev__attach(skel);
	if (!ASSERT_OK(err, "skel attach"))
		goto teardown;

	/* Activate dm-verity — triggers verity_preresume() hooks. */
	snprintf(cmd, sizeof(cmd),
		 "veritysetup open %s %s %s %s 2>/dev/null",
		 data_loop, DM_NAME, hash_loop, roothash);
	if (!ASSERT_OK(run_cmd(cmd, NULL, 0), "veritysetup open"))
		goto teardown;

	/* Get the dm device's dev_t. */
	if (!ASSERT_OK(stat(DM_DEV_PATH, &st), "stat dm dev"))
		goto remove_dm;

	dev_key = (__u32)st.st_rdev;

	/* Look up the device in the BPF map and verify. */
	err = bpf_map__lookup_elem(skel->maps.verity_devices,
				   &dev_key, sizeof(dev_key),
				   &val, sizeof(val), 0);
	if (!ASSERT_OK(err, "map lookup"))
		goto remove_dm;

	ASSERT_EQ(val.has_roothash, 1, "has_roothash");
	ASSERT_EQ(val.sig_valid, 0, "sig_valid (unsigned)");
	/*
	 * verity_preresume() always calls security_bdev_setintegrity()
	 * for the roothash. The signature-validity call only happens
	 * when CONFIG_DM_VERITY_VERIFY_ROOTHASH_SIG is enabled.
	 */
	ASSERT_GE(val.setintegrity_cnt, 1, "setintegrity_cnt min");
	ASSERT_LE(val.setintegrity_cnt, 2, "setintegrity_cnt max");

	/* Verify that the alloc hook fired at least once. */
	ASSERT_GT(skel->bss->alloc_count, 0, "alloc_count");

remove_dm:
	snprintf(cmd, sizeof(cmd), "dmsetup remove %s 2>/dev/null", DM_NAME);
	run_cmd(cmd, NULL, 0);

teardown:
	if (data_loop[0]) {
		snprintf(cmd, sizeof(cmd), "losetup -d %s 2>/dev/null",
			 data_loop);
		run_cmd(cmd, NULL, 0);
	}
	if (hash_loop[0]) {
		snprintf(cmd, sizeof(cmd), "losetup -d %s 2>/dev/null",
			 hash_loop);
		run_cmd(cmd, NULL, 0);
	}

cleanup:
	lsm_bdev__destroy(skel);
	if (data_fd >= 0)
		close(data_fd);
	if (hash_fd >= 0)
		close(hash_fd);
	unlink(data_img);
	unlink(hash_img);
}
