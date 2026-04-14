// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * GPIO character device helper for UAF tests.
 *
 * Copyright 2026 Google LLC
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CONFIGFS_DIR "/sys/kernel/config/gpio-sim"
#define PROCFS_DIR "/proc"

static void print_usage(void)
{
	printf("usage:\n");
	printf("  gpio-cdev-uaf [chip|handle|event|req] [poll|read|ioctl]\n");
}

static int _create_chip(const char *name, int create)
{
	char path[64];

	snprintf(path, sizeof(path), CONFIGFS_DIR "/%s", name);

	if (create)
		return mkdir(path, 0755);
	else
		return rmdir(path);
}

static int create_chip(const char *name)
{
	return _create_chip(name, 1);
}

static void remove_chip(const char *name)
{
	_create_chip(name, 0);
}

static int _create_bank(const char *chip_name, const char *name, int create)
{
	char path[64];

	snprintf(path, sizeof(path), CONFIGFS_DIR "/%s/%s", chip_name, name);

	if (create)
		return mkdir(path, 0755);
	else
		return rmdir(path);
}

static int create_bank(const char *chip_name, const char *name)
{
	return _create_bank(chip_name, name, 1);
}

static void remove_bank(const char *chip_name, const char *name)
{
	_create_bank(chip_name, name, 0);
}

static int _enable_chip(const char *name, int enable)
{
	char path[64];
	int fd, ret;

	snprintf(path, sizeof(path), CONFIGFS_DIR "/%s/live", name);

	fd = open(path, O_WRONLY);
	if (fd == -1)
		return fd;

	if (enable)
		ret = write(fd, "1", 1);
	else
		ret = write(fd, "0", 1);

	close(fd);
	return ret == 1 ? 0 : -1;
}

static int enable_chip(const char *name)
{
	return _enable_chip(name, 1);
}

static void disable_chip(const char *name)
{
	_enable_chip(name, 0);
}

static int open_chip(const char *chip_name, const char *bank_name)
{
	char path[64], dev_name[32];
	int ret, fd;

	ret = create_chip(chip_name);
	if (ret) {
		fprintf(stderr, "failed to create chip\n");
		return ret;
	}

	ret = create_bank(chip_name, bank_name);
	if (ret) {
		fprintf(stderr, "failed to create bank\n");
		goto err_remove_chip;
	}

	ret = enable_chip(chip_name);
	if (ret) {
		fprintf(stderr, "failed to enable chip\n");
		goto err_remove_bank;
	}

	snprintf(path, sizeof(path), CONFIGFS_DIR "/%s/%s/chip_name",
		 chip_name, bank_name);

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		ret = fd;
		fprintf(stderr, "failed to open %s\n", path);
		goto err_disable_chip;
	}

	ret = read(fd, dev_name, sizeof(dev_name) - 1);
	close(fd);
	if (ret == -1) {
		fprintf(stderr, "failed to read %s\n", path);
		goto err_disable_chip;
	}
	dev_name[ret] = '\0';
	if (ret && dev_name[ret - 1] == '\n')
		dev_name[ret - 1] = '\0';

	snprintf(path, sizeof(path), "/dev/%s", dev_name);

	fd = open(path, O_RDWR);
	if (fd == -1) {
		ret = fd;
		fprintf(stderr, "failed to open %s\n", path);
		goto err_disable_chip;
	}

	return fd;
err_disable_chip:
	disable_chip(chip_name);
err_remove_bank:
	remove_bank(chip_name, bank_name);
err_remove_chip:
	remove_chip(chip_name);
	return ret;
}

static void close_chip(const char *chip_name, const char *bank_name)
{
	disable_chip(chip_name);
	remove_bank(chip_name, bank_name);
	remove_chip(chip_name);
}

static int test_poll(int fd)
{
	struct pollfd pfds;

	pfds.fd = fd;
	pfds.events = POLLIN;
	pfds.revents = 0;

	if (poll(&pfds, 1, 0) == -1)
		return -1;

	return (pfds.revents & ~(POLLHUP | POLLERR)) ? -1 : 0;
}

static int test_read(int fd)
{
	char data;

	if (read(fd, &data, 1) == -1 && errno == ENODEV)
		return 0;
	return -1;
}

static int test_ioctl(int fd)
{
	if (ioctl(fd, 0, NULL) == -1 && errno == ENODEV)
		return 0;
	return -1;
}

int main(int argc, char **argv)
{
	int cfd, fd, ret;
	int (*test_func)(int);

	if (argc != 3) {
		print_usage();
		return EXIT_FAILURE;
	}

	if (strcmp(argv[1], "chip") == 0 ||
	    strcmp(argv[1], "event") == 0 ||
	    strcmp(argv[1], "req") == 0) {
		if (strcmp(argv[2], "poll") &&
		    strcmp(argv[2], "read") &&
		    strcmp(argv[2], "ioctl")) {
			fprintf(stderr, "unknown command: %s\n", argv[2]);
			return EXIT_FAILURE;
		}
	} else if (strcmp(argv[1], "handle") == 0) {
		if (strcmp(argv[2], "ioctl")) {
			fprintf(stderr, "unknown command: %s\n", argv[2]);
			return EXIT_FAILURE;
		}
	} else {
		fprintf(stderr, "unknown command: %s\n", argv[1]);
		return EXIT_FAILURE;
	}

	if (strcmp(argv[2], "poll") == 0)
		test_func = test_poll;
	else if (strcmp(argv[2], "read") == 0)
		test_func = test_read;
	else	/* strcmp(argv[2], "ioctl") == 0 */
		test_func = test_ioctl;

	cfd = open_chip("chip", "bank");
	if (cfd == -1) {
		fprintf(stderr, "failed to open chip\n");
		return EXIT_FAILURE;
	}

	/* Step 1: Hold a FD to the test target. */
	if (strcmp(argv[1], "chip") == 0) {
		fd = cfd;
	} else if (strcmp(argv[1], "handle") == 0) {
		struct gpiohandle_request req = {0};

		req.lines = 1;
		if (ioctl(cfd, GPIO_GET_LINEHANDLE_IOCTL, &req) == -1) {
			fprintf(stderr, "failed to get handle FD\n");
			goto err_close_chip;
		}

		close(cfd);
		fd = req.fd;
	} else if (strcmp(argv[1], "event") == 0) {
		struct gpioevent_request req = {0};

		if (ioctl(cfd, GPIO_GET_LINEEVENT_IOCTL, &req) == -1) {
			fprintf(stderr, "failed to get event FD\n");
			goto err_close_chip;
		}

		close(cfd);
		fd = req.fd;
	} else {	/* strcmp(argv[1], "req") == 0 */
		struct gpio_v2_line_request req = {0};

		req.num_lines = 1;
		if (ioctl(cfd, GPIO_V2_GET_LINE_IOCTL, &req) == -1) {
			fprintf(stderr, "failed to get req FD\n");
			goto err_close_chip;
		}

		close(cfd);
		fd = req.fd;
	}

	/* Step 2: Free the chip. */
	close_chip("chip", "bank");

	/* Step 3: Access the dangling FD to trigger UAF. */
	ret = test_func(fd);
	close(fd);
	return ret ? EXIT_FAILURE : EXIT_SUCCESS;
err_close_chip:
	close(cfd);
	close_chip("chip", "bank");
	return EXIT_FAILURE;
}
