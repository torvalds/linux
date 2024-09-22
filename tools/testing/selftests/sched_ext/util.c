/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2024 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2024 David Vernet <dvernet@meta.com>
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Returns read len on success, or -errno on failure. */
static ssize_t read_text(const char *path, char *buf, size_t max_len)
{
	ssize_t len;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -errno;

	len = read(fd, buf, max_len - 1);

	if (len >= 0)
		buf[len] = 0;

	close(fd);
	return len < 0 ? -errno : len;
}

/* Returns written len on success, or -errno on failure. */
static ssize_t write_text(const char *path, char *buf, ssize_t len)
{
	int fd;
	ssize_t written;

	fd = open(path, O_WRONLY | O_APPEND);
	if (fd < 0)
		return -errno;

	written = write(fd, buf, len);
	close(fd);
	return written < 0 ? -errno : written;
}

long file_read_long(const char *path)
{
	char buf[128];


	if (read_text(path, buf, sizeof(buf)) <= 0)
		return -1;

	return atol(buf);
}

int file_write_long(const char *path, long val)
{
	char buf[64];
	int ret;

	ret = sprintf(buf, "%lu", val);
	if (ret < 0)
		return ret;

	if (write_text(path, buf, sizeof(buf)) <= 0)
		return -1;

	return 0;
}
