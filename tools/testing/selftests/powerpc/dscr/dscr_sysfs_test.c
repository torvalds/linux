// SPDX-License-Identifier: GPL-2.0-only
/*
 * POWER Data Stream Control Register (DSCR) sysfs interface test
 *
 * This test updates to system wide DSCR default through the sysfs interface
 * and then verifies that all the CPU specific DSCR defaults are updated as
 * well verified from their sysfs interfaces.
 *
 * Copyright 2015, Anshuman Khandual, IBM Corporation.
 */
#include "dscr.h"

static int check_cpu_dscr_default(char *file, unsigned long val)
{
	char buf[10];
	int fd, rc;

	fd = open(file, O_RDWR);
	if (fd == -1) {
		perror("open() failed");
		return 1;
	}

	rc = read(fd, buf, sizeof(buf));
	if (rc == -1) {
		perror("read() failed");
		close(fd);
		return 1;
	}
	close(fd);

	buf[rc] = '\0';
	if (strtol(buf, NULL, 16) != val) {
		printf("DSCR match failed: %ld (system) %ld (cpu)\n",
					val, strtol(buf, NULL, 16));
		return 1;
	}
	return 0;
}

static int check_all_cpu_dscr_defaults(unsigned long val)
{
	DIR *sysfs;
	struct dirent *dp;
	char file[LEN_MAX];

	sysfs = opendir(CPU_PATH);
	if (!sysfs) {
		perror("opendir() failed");
		return 1;
	}

	while ((dp = readdir(sysfs))) {
		int len;

		if (!(dp->d_type & DT_DIR))
			continue;
		if (!strcmp(dp->d_name, "cpuidle"))
			continue;
		if (!strstr(dp->d_name, "cpu"))
			continue;

		len = snprintf(file, LEN_MAX, "%s%s/dscr", CPU_PATH, dp->d_name);
		if (len >= LEN_MAX)
			continue;
		if (access(file, F_OK))
			continue;

		if (check_cpu_dscr_default(file, val)) {
			closedir(sysfs);
			return 1;
		}
	}
	closedir(sysfs);
	return 0;
}

int dscr_sysfs(void)
{
	unsigned long orig_dscr_default;
	int i, j;

	SKIP_IF(!have_hwcap2(PPC_FEATURE2_DSCR));

	orig_dscr_default = get_default_dscr();
	for (i = 0; i < COUNT; i++) {
		for (j = 0; j < DSCR_MAX; j++) {
			set_default_dscr(j);
			if (check_all_cpu_dscr_defaults(j))
				goto fail;
		}
	}
	set_default_dscr(orig_dscr_default);
	return 0;
fail:
	set_default_dscr(orig_dscr_default);
	return 1;
}

int main(int argc, char *argv[])
{
	return test_harness(dscr_sysfs, "dscr_sysfs_test");
}
