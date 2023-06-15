// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Microsoft Corporation.
 *
 * Authors:
 *   Beau Belgrave <beaub@linux.microsoft.com>
 */

#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/user_events.h>

const char *data_file = "/sys/kernel/tracing/user_events_data";
int enabled = 0;

static int event_reg(int fd, const char *command, int *write, int *enabled)
{
	struct user_reg reg = {0};

	reg.size = sizeof(reg);
	reg.enable_bit = 31;
	reg.enable_size = sizeof(*enabled);
	reg.enable_addr = (__u64)enabled;
	reg.name_args = (__u64)command;

	if (ioctl(fd, DIAG_IOCSREG, &reg) == -1)
		return -1;

	*write = reg.write_index;

	return 0;
}

int main(int argc, char **argv)
{
	int data_fd, write;
	struct iovec io[2];
	__u32 count = 0;

	data_fd = open(data_file, O_RDWR);

	if (event_reg(data_fd, "test u32 count", &write, &enabled) == -1)
		return errno;

	/* Setup iovec */
	io[0].iov_base = &write;
	io[0].iov_len = sizeof(write);
	io[1].iov_base = &count;
	io[1].iov_len = sizeof(count);
ask:
	printf("Press enter to check status...\n");
	getchar();

	/* Check if anyone is listening */
	if (enabled) {
		/* Yep, trace out our data */
		writev(data_fd, (const struct iovec *)io, 2);

		/* Increase the count */
		count++;

		printf("Something was attached, wrote data\n");
	}

	goto ask;

	return 0;
}
