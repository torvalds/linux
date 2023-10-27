// SPDX-License-Identifier: GPL-2.0-only

#include <stdbool.h>
#include <stdlib.h>
#include <error.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "unpriv_helpers.h"

static bool get_mitigations_off(void)
{
	char cmdline[4096], *c;
	int fd, ret = false;

	fd = open("/proc/cmdline", O_RDONLY);
	if (fd < 0) {
		perror("open /proc/cmdline");
		return false;
	}

	if (read(fd, cmdline, sizeof(cmdline) - 1) < 0) {
		perror("read /proc/cmdline");
		goto out;
	}

	cmdline[sizeof(cmdline) - 1] = '\0';
	for (c = strtok(cmdline, " \n"); c; c = strtok(NULL, " \n")) {
		if (strncmp(c, "mitigations=off", strlen(c)))
			continue;
		ret = true;
		break;
	}
out:
	close(fd);
	return ret;
}

bool get_unpriv_disabled(void)
{
	bool disabled;
	char buf[2];
	FILE *fd;

	fd = fopen("/proc/sys/" UNPRIV_SYSCTL, "r");
	if (fd) {
		disabled = (fgets(buf, 2, fd) == buf && atoi(buf));
		fclose(fd);
	} else {
		perror("fopen /proc/sys/" UNPRIV_SYSCTL);
		disabled = true;
	}

	return disabled ? true : get_mitigations_off();
}
