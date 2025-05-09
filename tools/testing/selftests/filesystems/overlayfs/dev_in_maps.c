// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#define __SANE_USERSPACE_TYPES__ // Use ll64

#include <inttypes.h>
#include <unistd.h>
#include <stdio.h>

#include <linux/unistd.h>
#include <linux/types.h>
#include <linux/mount.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sched.h>
#include <fcntl.h>

#include "../../kselftest.h"
#include "log.h"
#include "../wrappers.h"

static long get_file_dev_and_inode(void *addr, struct statx *stx)
{
	char buf[4096];
	FILE *mapf;

	mapf = fopen("/proc/self/maps", "r");
	if (mapf == NULL)
		return pr_perror("fopen(/proc/self/maps)");

	while (fgets(buf, sizeof(buf), mapf)) {
		unsigned long start, end;
		uint32_t maj, min;
		__u64 ino;

		if (sscanf(buf, "%lx-%lx %*s %*s %x:%x %llu",
				&start, &end, &maj, &min, &ino) != 5)
			return pr_perror("unable to parse: %s", buf);
		if (start == (unsigned long)addr) {
			stx->stx_dev_major = maj;
			stx->stx_dev_minor = min;
			stx->stx_ino = ino;
			return 0;
		}
	}

	return pr_err("unable to find the mapping");
}

static int ovl_mount(void)
{
	int tmpfs, fsfd, ovl;

	fsfd = sys_fsopen("tmpfs", 0);
	if (fsfd == -1)
		return pr_perror("fsopen(tmpfs)");

	if (sys_fsconfig(fsfd, FSCONFIG_CMD_CREATE, NULL, NULL, 0) == -1)
		return pr_perror("FSCONFIG_CMD_CREATE");

	tmpfs = sys_fsmount(fsfd, 0, 0);
	if (tmpfs == -1)
		return pr_perror("fsmount");

	close(fsfd);

	/* overlayfs can't be constructed on top of a detached mount. */
	if (sys_move_mount(tmpfs, "", AT_FDCWD, "/tmp", MOVE_MOUNT_F_EMPTY_PATH))
		return pr_perror("move_mount");
	close(tmpfs);

	if (mkdir("/tmp/w", 0755) == -1 ||
	    mkdir("/tmp/u", 0755) == -1 ||
	    mkdir("/tmp/l", 0755) == -1)
		return pr_perror("mkdir");

	fsfd = sys_fsopen("overlay", 0);
	if (fsfd == -1)
		return pr_perror("fsopen(overlay)");
	if (sys_fsconfig(fsfd, FSCONFIG_SET_STRING, "source", "test", 0) == -1 ||
	    sys_fsconfig(fsfd, FSCONFIG_SET_STRING, "lowerdir", "/tmp/l", 0) == -1 ||
	    sys_fsconfig(fsfd, FSCONFIG_SET_STRING, "upperdir", "/tmp/u", 0) == -1 ||
	    sys_fsconfig(fsfd, FSCONFIG_SET_STRING, "workdir", "/tmp/w", 0) == -1)
		return pr_perror("fsconfig");
	if (sys_fsconfig(fsfd, FSCONFIG_CMD_CREATE, NULL, NULL, 0) == -1)
		return pr_perror("fsconfig");
	ovl = sys_fsmount(fsfd, 0, 0);
	if (ovl == -1)
		return pr_perror("fsmount");

	return ovl;
}

/*
 * Check that the file device and inode shown in /proc/pid/maps match values
 * returned by stat(2).
 */
static int test(void)
{
	struct statx stx, mstx;
	int ovl, fd;
	void *addr;

	ovl = ovl_mount();
	if (ovl == -1)
		return -1;

	fd = openat(ovl, "test", O_RDWR | O_CREAT, 0644);
	if (fd == -1)
		return pr_perror("openat");

	addr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED)
		return pr_perror("mmap");

	if (get_file_dev_and_inode(addr, &mstx))
		return -1;
	if (statx(fd, "", AT_EMPTY_PATH | AT_STATX_SYNC_AS_STAT, STATX_INO, &stx))
		return pr_perror("statx");

	if (stx.stx_dev_major != mstx.stx_dev_major ||
	    stx.stx_dev_minor != mstx.stx_dev_minor ||
	    stx.stx_ino != mstx.stx_ino)
		return pr_fail("unmatched dev:ino %x:%x:%llx (expected %x:%x:%llx)\n",
			mstx.stx_dev_major, mstx.stx_dev_minor, mstx.stx_ino,
			stx.stx_dev_major, stx.stx_dev_minor, stx.stx_ino);

	ksft_test_result_pass("devices are matched\n");
	return 0;
}

int main(int argc, char **argv)
{
	int fsfd;

	fsfd = sys_fsopen("overlay", 0);
	if (fsfd == -1) {
		ksft_test_result_skip("unable to create overlay mount\n");
		return 1;
	}
	close(fsfd);

	/* Create a new mount namespace to not care about cleaning test mounts. */
	if (unshare(CLONE_NEWNS) == -1) {
		ksft_test_result_skip("unable to create a new mount namespace\n");
		return 1;
	}
	if (sys_mount(NULL, "/", NULL, MS_SLAVE | MS_REC, NULL) == -1) {
		pr_perror("mount");
		return 1;
	}

	ksft_set_plan(1);

	if (test())
		return 1;

	ksft_exit_pass();
	return 0;
}
