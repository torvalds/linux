// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/android/binder.h>
#include <linux/android/binderfs.h>

int main(int argc, char *argv[])
{
	int fd, ret, saved_errno;
	size_t len;
	struct binderfs_device device = { 0 };

	ret = unshare(CLONE_NEWNS);
	if (ret < 0) {
		fprintf(stderr, "%s - Failed to unshare mount namespace\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	ret = mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, 0);
	if (ret < 0) {
		fprintf(stderr, "%s - Failed to mount / as private\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	ret = mkdir("/dev/binderfs", 0755);
	if (ret < 0 && errno != EEXIST) {
		fprintf(stderr, "%s - Failed to create binderfs mountpoint\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	ret = mount(NULL, "/dev/binderfs", "binder", 0, 0);
	if (ret < 0) {
		fprintf(stderr, "%s - Failed to mount binderfs\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	memcpy(device.name, "my-binder", strlen("my-binder"));

	fd = open("/dev/binderfs/binder-control", O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "%s - Failed to open binder-control device\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	ret = ioctl(fd, BINDER_CTL_ADD, &device);
	saved_errno = errno;
	close(fd);
	errno = saved_errno;
	if (ret < 0) {
		fprintf(stderr, "%s - Failed to allocate new binder device\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	printf("Allocated new binder device with major %d, minor %d, and name %s\n",
	       device.major, device.minor, device.name);

	ret = unlink("/dev/binderfs/my-binder");
	if (ret < 0) {
		fprintf(stderr, "%s - Failed to delete binder device\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Cleanup happens when the mount namespace dies. */
	exit(EXIT_SUCCESS);
}
