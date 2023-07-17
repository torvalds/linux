// SPDX-License-Identifier: GPL-2.0-only

#include <stdbool.h>
#include <stdlib.h>
#include <error.h>
#include <stdio.h>

#include "unpriv_helpers.h"

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

	return disabled;
}
