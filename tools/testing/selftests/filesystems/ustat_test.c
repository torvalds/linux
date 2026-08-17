// SPDX-License-Identifier: GPL-2.0
/*
 * Test ustat(2): looking up superblocks by device number.
 *
 * ustat() resolves a device number to a mounted superblock via
 * user_get_super(). Check that the device number of a mounted tmpfs (an
 * anonymous device) resolves, that it stops resolving once the filesystem
 * is unmounted and that bogus device numbers report EINVAL.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "../kselftest_harness.h"

/* struct ustat is not exported through UAPI, mirror include/linux/types.h. */
struct ustat_buf {
	int		f_tfree;
	unsigned long	f_tinode;
	char		f_fname[6];
	char		f_fpack[6];
	/* slack in case an architecture lays the struct out differently */
	char		pad[64];
};

#ifdef __NR_ustat

/*
 * The kernel decodes @dev with new_decode_dev(), which matches the low 32
 * bits of the st_dev encoding stat(2) returns for any major below 4096.
 */
static int sys_ustat(unsigned int dev, struct ustat_buf *buf)
{
	return syscall(__NR_ustat, dev, buf);
}

static int write_string(const char *path, const char *string)
{
	ssize_t len = strlen(string);
	int fd;

	fd = open(path, O_WRONLY);
	if (fd < 0)
		return -1;
	if (write(fd, string, len) != len) {
		close(fd);
		return -1;
	}
	return close(fd);
}

/* Enter namespaces in which mounting a tmpfs instance is allowed. */
static int setup_namespaces(void)
{
	uid_t uid = getuid();
	gid_t gid = getgid();
	char map[64];

	if (unshare(CLONE_NEWNS | (uid ? CLONE_NEWUSER : 0)))
		return -1;

	if (uid) {
		if (write_string("/proc/self/setgroups", "deny"))
			return -1;
		snprintf(map, sizeof(map), "0 %d 1", uid);
		if (write_string("/proc/self/uid_map", map))
			return -1;
		snprintf(map, sizeof(map), "0 %d 1", gid);
		if (write_string("/proc/self/gid_map", map))
			return -1;
	}

	return mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL);
}

TEST(resolves_mounted_superblock)
{
	char dir[] = "/tmp/ustat_test.XXXXXX";
	struct ustat_buf ub;
	struct stat st;

	ASSERT_NE(NULL, mkdtemp(dir));

	if (setup_namespaces()) {
		rmdir(dir);
		SKIP(return, "cannot set up namespaces: %s", strerror(errno));
	}

	ASSERT_EQ(0, mount("ustat_test", dir, "tmpfs", 0, NULL));
	ASSERT_EQ(0, stat(dir, &st));

	memset(&ub, 0xff, sizeof(ub));
	ASSERT_EQ(0, sys_ustat(st.st_dev, &ub))
		TH_LOG("ustat(%u): %s", (unsigned int)st.st_dev,
		       strerror(errno));

	ASSERT_EQ(0, umount(dir));

	/* The unmount removed the superblock, the device is gone. */
	ASSERT_EQ(-1, sys_ustat(st.st_dev, &ub));
	ASSERT_EQ(EINVAL, errno);

	rmdir(dir);
}

TEST(bogus_device_numbers)
{
	struct ustat_buf ub;

	ASSERT_EQ(-1, sys_ustat(0, &ub));
	ASSERT_EQ(EINVAL, errno);

	/* major 4095, minor 1048575: nothing plausible lives there */
	ASSERT_EQ(-1, sys_ustat((0xfffu << 8) | 0xffu | (0xfff00u << 12), &ub));
	ASSERT_EQ(EINVAL, errno);
}

#else /* !__NR_ustat */

TEST(unsupported)
{
	SKIP(return, "ustat(2) is not available on this architecture");
}

#endif /* __NR_ustat */

TEST_HARNESS_MAIN
