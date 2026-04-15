// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "sysctl_helpers.h"
#include "test_progs.h"

int sysctl_set(const char *sysctl_path, char *old_val, const char *new_val)
{
	int ret = 0;
	FILE *fp;

	fp = fopen(sysctl_path, "r+");
	if (!fp)
		return -errno;
	if (old_val && fscanf(fp, "%s", old_val) <= 0) {
		ret = -ENOENT;
	} else if (!old_val || strcmp(old_val, new_val) != 0) {
		fseek(fp, 0, SEEK_SET);
		if (fprintf(fp, "%s", new_val) < 0)
			ret = -errno;
	}
	fclose(fp);

	return ret;
}

int sysctl_set_or_fail(const char *sysctl_path, char *old_val, const char *new_val)
{
	int err;

	err = sysctl_set(sysctl_path, old_val, new_val);
	if (err)
		PRINT_FAIL("failed to set %s to %s: %s\n", sysctl_path, new_val, strerror(-err));
	return err;
}
