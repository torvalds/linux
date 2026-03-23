// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test: rootfs overmounted multiple times with chroot into topmost
 *
 * This test creates a scenario where:
 * 1. A new mount namespace is created with a tmpfs root (via pivot_root)
 * 2. A mountpoint is created and overmounted multiple times
 * 3. The caller chroots into the topmost mount layer
 *
 * The test verifies that:
 * - Multiple overmounts create separate mount layers
 * - Each layer's files are isolated
 * - chroot correctly sets the process's root to the topmost layer
 * - After chroot, only the topmost layer's files are visible
 *
 * Copyright (c) 2024 Christian Brauner <brauner@kernel.org>
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/mount.h>
#include <linux/stat.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../utils.h"
#include "empty_mntns.h"
#include "kselftest_harness.h"

#define NR_OVERMOUNTS 5

/*
 * Setup a proper root filesystem using pivot_root.
 * This ensures we own the root directory in our user namespace.
 */
static int setup_root(void)
{
	char tmpdir[] = "/tmp/overmount_test.XXXXXX";
	char oldroot[256];

	if (!mkdtemp(tmpdir))
		return -1;

	/* Mount tmpfs at the temporary directory */
	if (mount("tmpfs", tmpdir, "tmpfs", 0, "size=10M"))
		return -1;

	/* Create directory for old root */
	snprintf(oldroot, sizeof(oldroot), "%s/oldroot", tmpdir);
	if (mkdir(oldroot, 0755))
		return -1;

	/* pivot_root to use the tmpfs as new root */
	if (syscall(SYS_pivot_root, tmpdir, oldroot))
		return -1;

	if (chdir("/"))
		return -1;

	/* Unmount old root */
	if (umount2("/oldroot", MNT_DETACH))
		return -1;

	/* Remove oldroot directory */
	if (rmdir("/oldroot"))
		return -1;

	return 0;
}

/*
 * Test scenario:
 * 1. Enter a user namespace to gain CAP_SYS_ADMIN
 * 2. Create a new mount namespace
 * 3. Setup a tmpfs root via pivot_root
 * 4. Create a mountpoint /newroot and overmount it multiple times
 * 5. Create a marker file in each layer
 * 6. Chroot into /newroot (the topmost overmount)
 * 7. Verify we're in the topmost layer (only topmost marker visible)
 */
TEST(overmount_chroot)
{
	pid_t pid;

	pid = fork();
	ASSERT_GE(pid, 0);

	if (pid == 0) {
		ssize_t nr_mounts;
		uint64_t mnt_ids[NR_OVERMOUNTS + 1];
		uint64_t root_id_before, root_id_after;
		struct statmount *sm;
		char marker[64];
		int fd, i;

		/* Step 1: Enter user namespace for privileges */
		if (enter_userns())
			_exit(1);

		/* Step 2: Create a new mount namespace */
		if (unshare(CLONE_NEWNS))
			_exit(2);

		/* Step 3: Make the mount tree private */
		if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL))
			_exit(3);

		/* Step 4: Setup a proper tmpfs root via pivot_root */
		if (setup_root())
			_exit(4);

		/* Create the base mount point for overmounting */
		if (mkdir("/newroot", 0755))
			_exit(5);

		/* Mount base tmpfs on /newroot */
		if (mount("tmpfs", "/newroot", "tmpfs", 0, "size=1M"))
			_exit(6);

		/* Record base mount ID */
		mnt_ids[0] = get_unique_mnt_id("/newroot");
		if (!mnt_ids[0])
			_exit(7);

		/* Create marker in base layer */
		fd = open("/newroot/layer_0", O_CREAT | O_RDWR, 0644);
		if (fd < 0)
			_exit(8);
		if (write(fd, "layer_0", 7) != 7) {
			close(fd);
			_exit(9);
		}
		close(fd);

		/* Step 5: Overmount /newroot multiple times with tmpfs */
		for (i = 0; i < NR_OVERMOUNTS; i++) {
			if (mount("tmpfs", "/newroot", "tmpfs", 0, "size=1M"))
				_exit(10);

			/* Record mount ID for this layer */
			mnt_ids[i + 1] = get_unique_mnt_id("/newroot");
			if (!mnt_ids[i + 1])
				_exit(11);

			/* Create a marker file in each layer */
			snprintf(marker, sizeof(marker), "/newroot/layer_%d", i + 1);
			fd = open(marker, O_CREAT | O_RDWR, 0644);
			if (fd < 0)
				_exit(12);

			if (write(fd, marker, strlen(marker)) != (ssize_t)strlen(marker)) {
				close(fd);
				_exit(13);
			}
			close(fd);
		}

		/* Verify mount count increased */
		nr_mounts = count_mounts();
		if (nr_mounts < NR_OVERMOUNTS + 2)
			_exit(14);

		/* Record root mount ID before chroot */
		root_id_before = get_unique_mnt_id("/newroot");

		/* Verify this is the topmost layer's mount */
		if (root_id_before != mnt_ids[NR_OVERMOUNTS])
			_exit(15);

		/* Step 6: Chroot into /newroot (the topmost overmount) */
		if (chroot("/newroot"))
			_exit(16);

		/* Change to root directory within the chroot */
		if (chdir("/"))
			_exit(17);

		/* Step 7: Verify we're in the topmost layer */
		root_id_after = get_unique_mnt_id("/");

		/* The mount ID should be the same as the topmost layer */
		if (root_id_after != mnt_ids[NR_OVERMOUNTS])
			_exit(18);

		/* Verify the topmost layer's marker file exists */
		snprintf(marker, sizeof(marker), "/layer_%d", NR_OVERMOUNTS);
		if (access(marker, F_OK))
			_exit(19);

		/* Verify we cannot see markers from lower layers (they're hidden) */
		for (i = 0; i < NR_OVERMOUNTS; i++) {
			snprintf(marker, sizeof(marker), "/layer_%d", i);
			if (access(marker, F_OK) == 0)
				_exit(20);
		}

		/* Verify the root mount is tmpfs */
		sm = statmount_alloc(root_id_after, 0,
				     STATMOUNT_MNT_BASIC | STATMOUNT_MNT_ROOT |
				     STATMOUNT_MNT_POINT | STATMOUNT_FS_TYPE, 0);
		if (!sm)
			_exit(21);

		if (sm->mask & STATMOUNT_FS_TYPE) {
			if (strcmp(sm->str + sm->fs_type, "tmpfs") != 0) {
				free(sm);
				_exit(22);
			}
		}

		free(sm);
		_exit(0);
	}

	ASSERT_EQ(wait_for_pid(pid), 0);
}

TEST_HARNESS_MAIN
