// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/fs.h>

static int set_immutable(const char *path, int immutable)
{
	unsigned int flags;
	int fd;
	int rc;
	int error;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return fd;

	rc = ioctl(fd, FS_IOC_GETFLAGS, &flags);
	if (rc < 0) {
		error = errno;
		close(fd);
		errno = error;
		return rc;
	}

	if (immutable)
		flags |= FS_IMMUTABLE_FL;
	else
		flags &= ~FS_IMMUTABLE_FL;

	rc = ioctl(fd, FS_IOC_SETFLAGS, &flags);
	error = errno;
	close(fd);
	errno = error;
	return rc;
}

static int get_immutable(const char *path)
{
	unsigned int flags;
	int fd;
	int rc;
	int error;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return fd;

	rc = ioctl(fd, FS_IOC_GETFLAGS, &flags);
	if (rc < 0) {
		error = errno;
		close(fd);
		errno = error;
		return rc;
	}
	close(fd);
	if (flags & FS_IMMUTABLE_FL)
		return 1;
	return 0;
}

int main(int argc, char **argv)
{
	const char *path;
	char buf[5];
	int fd, rc;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <path>\n", argv[0]);
		return EXIT_FAILURE;
	}

	path = argv[1];

	/* attributes: EFI_VARIABLE_NON_VOLATILE |
	 *		EFI_VARIABLE_BOOTSERVICE_ACCESS |
	 *		EFI_VARIABLE_RUNTIME_ACCESS
	 */
	*(uint32_t *)buf = 0x7;
	buf[4] = 0;

	/* create a test variable */
	fd = open(path, O_WRONLY | O_CREAT, 0600);
	if (fd < 0) {
		perror("open(O_WRONLY)");
		return EXIT_FAILURE;
	}

	rc = write(fd, buf, sizeof(buf));
	if (rc != sizeof(buf)) {
		perror("write");
		return EXIT_FAILURE;
	}

	close(fd);

	rc = get_immutable(path);
	if (rc < 0) {
		perror("ioctl(FS_IOC_GETFLAGS)");
		return EXIT_FAILURE;
	} else if (rc) {
		rc = set_immutable(path, 0);
		if (rc < 0) {
			perror("ioctl(FS_IOC_SETFLAGS)");
			return EXIT_FAILURE;
		}
	}

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return EXIT_FAILURE;
	}

	if (unlink(path) < 0) {
		perror("unlink");
		return EXIT_FAILURE;
	}

	rc = read(fd, buf, sizeof(buf));
	if (rc > 0) {
		fprintf(stderr, "reading from an unlinked variable "
				"shouldn't be possible\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
